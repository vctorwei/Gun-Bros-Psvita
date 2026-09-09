#include "utils/classic_dialog.h"
#include <psp2/message_dialog.h>
#include <psp2/display.h>
#include <vitaGL.h>
#include <stdio.h>
#include <string.h>
static char pending[512];
static GunBrosDialogAction pending_action;
void gunbros_classic_dialog(const char *message, GunBrosDialogAction action) {
    if (pending[0]) return;
    snprintf(pending, sizeof(pending), "%s", message);
    pending_action = action;
}
/* Called outside native callbacks, with a finished game frame as background. */
void gunbros_classic_dialog_service(void) {
    while (pending[0]) {
        SceMsgDialogUserMessageParam user;
        SceMsgDialogParam param;
        memset(&user, 0, sizeof(user));
        sceMsgDialogParamInit(&param);
        user.buttonType = pending_action ? SCE_MSG_DIALOG_BUTTON_TYPE_YESNO : SCE_MSG_DIALOG_BUTTON_TYPE_OK;
        user.msg = (const SceChar8 *)pending;
        param.mode = SCE_MSG_DIALOG_MODE_USER_MSG;
        param.userMsgParam = &user;
        if (sceMsgDialogInit(&param) < 0) return; /* Retry next frame. */
        while (sceMsgDialogGetStatus() != SCE_COMMON_DIALOG_STATUS_FINISHED) {
            vglSwapBuffers(GL_TRUE);
            sceDisplayWaitVblankStart();
        }
        SceMsgDialogResult result;
        memset(&result, 0, sizeof(result));
        sceMsgDialogGetResult(&result);
        sceMsgDialogTerm();
        GunBrosDialogAction action = pending_action;
        pending[0] = 0;
        pending_action = NULL;
        if (action && result.buttonId == SCE_MSG_DIALOG_BUTTON_ID_YES) {
            const char *message = action();
            if (message) gunbros_classic_dialog(message, NULL);
        }
    }
}
