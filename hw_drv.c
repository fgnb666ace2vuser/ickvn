#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/utsname.h>
#include <linux/seq_file.h>
#include <linux/string.h>

// ========== 自定义伪装参数 ==========
// 内核版本：保留6.12大版本，避免和安卓16系统出现版本矛盾，只改后缀
#define FAKE_KERNEL     "6.12.38-huawei-kirin"
// 架构：不用改，所有arm64机型都是这个
#define FAKE_ARCH       "aarch64"
// /proc/cpuinfo 里的 Hardware 字段
#define FAKE_HARDWARE   "kirin9030s"
// /proc/cpuinfo 里的 Processor 字段
#define FAKE_CPU_MODEL  "HUAWEI Kirin 9030S"
// ======================================

// 模块参数：是否开启自隐藏，默认开启，测试时可设为0方便查看
static bool hide = true;
module_param(hide, bool, 0644);
MODULE_PARM_DESC(hide, "Hide module from /proc/modules (default: true)");

// kallsyms_lookup_name 函数指针（用于查找内核符号）
typedef unsigned long (*kallsyms_lookup_name_t)(const char *name);
static kallsyms_lookup_name_t kallsyms_lookup_name_fn;

// cpuinfo hook 相关变量
static struct kprobe kp_cpuinfo;
static struct seq_file *saved_cpuinfo_m;

// 获取kallsyms_lookup_name函数地址
static int get_kallsyms_lookup_name(void)
{
    struct kprobe kp = {
        .symbol_name = "kallsyms_lookup_name",
    };
    int ret;

    ret = register_kprobe(&kp);
    if (ret) {
        pr_err("[hw_drv] 无法获取kallsyms_lookup_name: %d\n", ret);
        return ret;
    }

    kallsyms_lookup_name_fn = (kallsyms_lookup_name_t)kp.addr;
    unregister_kprobe(&kp);
    return 0;
}

// 伪装内核版本：直接修改内核全局utsname变量，uname和/proc/version统一生效
static int spoof_utsname(void)
{
    struct new_utsname *init_uts;

    init_uts = (struct new_utsname *)kallsyms_lookup_name_fn("init_uts_ns");
    if (!init_uts) {
        pr_err("[hw_drv] 无法找到init_uts_ns符号\n");
        return -EINVAL;
    }

    strscpy(init_uts->release, FAKE_KERNEL, __NEW_UTS_LEN);
    strscpy(init_uts->machine, FAKE_ARCH, __NEW_UTS_LEN);
    strscpy(init_uts->version, FAKE_HARDWARE, __NEW_UTS_LEN);

    pr_info("[hw_drv] 内核版本伪装完成\n");
    return 0;
}

// cpuinfo_show执行前：保存seq_file指针
static int hook_cpuinfo_show_pre(struct kprobe *p, struct pt_regs *regs)
{
    saved_cpuinfo_m = (struct seq_file *)regs->regs[0];
    return 0;
}

// cpuinfo_show执行后：修改输出缓冲区中的CPU信息
static void hook_cpuinfo_show_post(struct kprobe *p, struct pt_regs *regs, unsigned long flags)
{
    struct seq_file *m = saved_cpuinfo_m;
    char *buf, *pos, *line_end;
    char new_line[128];
    size_t old_len, new_len;

    if (!m || !m->buf || m->count == 0)
        goto out;

    buf = m->buf;

    // 替换 Hardware 行
    pos = strstr(buf, "Hardware\t: ");
    if (pos) {
        line_end = strchr(pos, '\n');
        if (line_end) {
            snprintf(new_line, sizeof(new_line), "Hardware\t: %s\n", FAKE_HARDWARE);
            old_len = line_end - pos + 1;
            new_len = strlen(new_line);

            if (new_len <= old_len) {
                memcpy(pos, new_line, new_len);
                if (new_len < old_len) {
                    memmove(pos + new_len, line_end + 1, m->count - (line_end + 1 - buf));
                    m->count -= (old_len - new_len);
                }
            }
        }
    }

    // 替换第一行 Processor 字段
    pos = strstr(buf, "Processor\t: ");
    if (pos) {
        line_end = strchr(pos, '\n');
        if (line_end) {
            snprintf(new_line, sizeof(new_line), "Processor\t: %s rev 0 (aarch64)\n", FAKE_CPU_MODEL);
            old_len = line_end - pos + 1;
            new_len = strlen(new_line);

            if (new_len <= old_len) {
                memcpy(pos, new_line, new_len);
                if (new_len < old_len) {
                    memmove(pos + new_len, line_end + 1, m->count - (line_end + 1 - buf));
                    m->count -= (old_len - new_len);
                }
            }
        }
    }

out:
    saved_cpuinfo_m = NULL;
}

static int __init hw_drv_init(void)
{
    int ret;

    // 1. 获取内核符号查找函数
    ret = get_kallsyms_lookup_name();
    if (ret)
        return ret;

    // 2. 伪装内核版本（uname + /proc/version 统一生效）
    ret = spoof_utsname();
    if (ret)
        return ret;

    // 3. 注册cpuinfo输出劫持
    kp_cpuinfo.symbol_name = "cpuinfo_show";
    kp_cpuinfo.pre_handler = hook_cpuinfo_show_pre;
    kp_cpuinfo.post_handler = hook_cpuinfo_show_post;
    ret = register_kprobe(&kp_cpuinfo);
    if (ret) {
        pr_err("[hw_drv] cpuinfo hook注册失败: %d\n", ret);
        pr_err("[hw_drv] 请执行: cat /proc/kallsyms | grep cpuinfo 确认正确符号名\n");
        return ret;
    }

    // 4. 模块自隐藏（可选）
    if (hide) {
        list_del_init(&THIS_MODULE->list);
        pr_info("[hw_drv] 模块已从/proc/modules隐藏\n");
    }

    pr_info("[hw_drv] 硬件信息伪装模块加载成功\n");
    return 0;
}

static void __exit hw_drv_exit(void)
{
    unregister_kprobe(&kp_cpuinfo);
    pr_info("[hw_drv] 模块已卸载（内核版本伪装需重启恢复）\n");
}

module_init(hw_drv_init);
module_exit(hw_drv_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Hardware info driver");
MODULE_AUTHOR("hw");