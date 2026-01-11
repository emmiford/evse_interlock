#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <SEGGER_RTT.h>

LOG_MODULE_REGISTER(hello_rtt, LOG_LEVEL_INF);

static void rtt_setup(void)
{
    // Channel 0 is typically used by Zephyr's RTT console/log backend.
    // We'll use channel 1 for "sideband" output + down/input.
    static char up1_buf[256];
    static char down1_buf[64];

    SEGGER_RTT_ConfigUpBuffer(1, "up1", up1_buf, sizeof(up1_buf),
                             SEGGER_RTT_MODE_NO_BLOCK_SKIP);
    SEGGER_RTT_ConfigDownBuffer(1, "down1", down1_buf, sizeof(down1_buf),
                               SEGGER_RTT_MODE_NO_BLOCK_SKIP);
}

int main(void)
{
    rtt_setup();

    uint32_t n = 0;
    char buf[64];

    printk("RTT hello_rtt boot (ch0 printk)\n");
    LOG_INF("RTT hello_rtt boot (LOG_INF)");

    while (1) {
        // Heartbeat on ch0 (printk) and ch1 (SEGGER RTT direct)
        printk("hb %u (ch0)\n", n);

        /* Smallest change: avoid SEGGER_RTT_printf (not linked in this build). */
        SEGGER_RTT_WriteString(1, "hb (ch1)\n");

        // Best-effort down/input from ch1
        int r = SEGGER_RTT_Read(1, buf, sizeof(buf) - 1);
        if (r > 0) {
            buf[r] = '\0';
            printk("RX ch1 (%d): %s\n", r, buf);
        }

        n++;
        k_sleep(K_MSEC(1000));
    }
}
