#ifndef GUNBROS_CLASSIC_DIALOG_H
#define GUNBROS_CLASSIC_DIALOG_H
typedef const char *(*GunBrosDialogAction)(void);
void gunbros_classic_dialog(const char *message, GunBrosDialogAction action);
void gunbros_classic_dialog_service(void);
#endif
