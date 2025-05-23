// Copyright (C) 2006-2009 by Tor Andersson.
// Copyright (C) 2023 by Adrian Welcker.
//
// This file is part of Gargoyle.
//
// Gargoyle is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// Gargoyle is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Gargoyle; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

#include <stdlib.h>
#include <string.h>

#include "glk.h"
#include "glkstart.h"

int main(int argc, char *argv[])
{
    garglk_startup(argc, argv);

    glkunix_startup_t startdata;
    startdata.argc = argc;
    startdata.argv = malloc((argc + 1) * sizeof(char *));
    memcpy(startdata.argv, argv, (argc + 1) * sizeof(char *));

    if (!glkunix_startup_code(&startdata)) {
        glk_exit();
    }

    glk_main();
    glk_exit();

    return 0;
}
