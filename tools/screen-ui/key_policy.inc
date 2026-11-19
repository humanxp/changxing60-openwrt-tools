static long cx_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* Timestamp of the previous power-key press while the panel is dark, for the double-press wake. */
static long cx_power_ms = 0;

/* Wake without resurrecting the "off" step: a wake must always produce a visible panel. */
static void panel_wake(void)
{
	bl_idle_dark = 0;
	if (bl_levels[bl_index] == 0) bl_index = 0;
	backlight_apply();
	/* Without this the idle timer is still past its deadline and the next tick blanks the panel
	 * again in the same second, which looks exactly like "the wake key did nothing". */
	bl_idle_seen = time(NULL);
}

static void handle_key(__u16 code, __s32 value)
{
	if (value != 1) return;                 /* press only: ignore release and autorepeat */

	/* A call owns the two volume keys while it is up: volume up answers, volume down ends it. */
	if (voice_active()) {
		if (code == KEY_VOLUMEUP && strcmp(vc_state, "ringing") == 0) {
			voice_cmd_run("answer");
			return;
		}
		if (code == KEY_VOLUMEDOWN) {
			voice_cmd_run("hangup");
			return;
		}
	}

	/* The panel counts as dark both when the idle timer blanked it and when the user stepped the
	 * brightness to the "off" step.  While dark only a double press of the power key wakes it:
	 * touch is ignored in touch_tap(), and volume keys are ignored here, so a pocket press cannot
	 * light up the only display the device has. */
	if (bl_idle_dark || !screen_on) {
		if (code != KEY_POWER) return;
		cx_power_ms = 0;
		panel_wake();
		return;
	}

	bl_idle_seen = time(NULL);

	if (code == KEY_POWER) {
		/* Single press while lit: blank now, keep the brightness step for the next wake. */
		bl_idle_dark = 1;
		backlight_write_pct(0);
		fprintf(stderr, "changxing60-screen: power key, backlight off (step %d%% kept)\n",
			bl_levels[bl_index]);
	} else if (code == KEY_VOLUMEUP) {
		/* Volume up = brighter (bl_levels is brightest first). */
		if (bl_index > 0) bl_index--;
		backlight_apply();
		backlight_save();
	} else if (code == KEY_VOLUMEDOWN) {
		/* Volume down = dimmer.  Stepping all the way down turns the backlight off; the next
		 * double press of the power key wakes at full brightness. */
		if (bl_index < BL_N - 1) bl_index++;
		backlight_apply();
		backlight_save();
	}
}
