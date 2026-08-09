#ifndef PM_METAL_SHELL_UI_H_
#define PM_METAL_SHELL_UI_H_

#ifdef __cplusplus
extern "C" {
#endif

/* Bind VT/F1 draw surface to gfx shadow; attach as console #0 viewport. */
int pm_metal_shell_ui_attach_console0(void);
int pm_metal_shell_ui_present(void);

#ifdef __cplusplus
}
#endif

#endif
