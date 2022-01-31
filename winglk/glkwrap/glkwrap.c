#include <stddef.h>

#include <windows.h>

#include <glk.h>
#include <glkstart.h>
#include <WinGlk.h>

strid_t glkunix_stream_open_pathname(char *pathname, glui32 writemode, glui32 rock)
{
    frefid_t ref = winglk_fileref_create_by_name(fileusage_BinaryMode | fileusage_Data, pathname, 0, 0);

    if (ref == NULL) {
        return NULL;
    }

    strid_t stream = glk_stream_open_file(ref, filemode_Read, rock);
    glk_fileref_destroy(ref);
    return stream;
}

void glkunix_set_base_file(char *filename)
{
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previnstance, LPSTR cmdline, int cmdshow)
{
    if (!InitGlk(0x00000700)) {
        exit(EXIT_FAILURE);
    }

    glkunix_startup_t data = {
        .argc = __argc,
        .argv = __argv,
    };

    glkunix_startup_code(&data);

    glk_main();
    glk_exit();

    return 0;
}
