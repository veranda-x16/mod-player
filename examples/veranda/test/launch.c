/* test/launch.c -- the player's gate (tools/gate_vmodplay.sh): what the
   shell does on a double click on a .mod (Veranda V48): the document's
   folder in curDirName, the application started from the top folder with
   GetFile(app, ST_LD_DATA, 0, name, 0). */
#include <veranda.h>
#include <string.h>
int main(void)
{
    strcpy((char *)curDirName, "/Mods");
    LogText("LAUNCH MOD Player DIZZYCRS.MOD");
    GetFile("MOD Player", ST_LD_DATA, 0, "DIZZYCRS.MOD", 0);
    LogText("LAUNCH FAILED");
    return 0;
}
