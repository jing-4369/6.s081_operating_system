#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/param.h"
#include "user/user.h"

int main(int argc, char **argv)
{
    int pid, buf_index = 0;
    char *args[MAXARG], arg[1024];
    char buf;
    if (argc < 2)
    {
        fprintf(2, "usage: xargs command ...");
        exit(0);
    }

    for (int i = 1; i < argc; i++)
    {
        args[i - 1] = argv[i];
    }

    while (read(0, &buf, 1) > 0)
    {
        if (buf == '\n')
        {
            arg[buf_index] = 0;
            if ((pid = fork()) == 0)
            {   
                args[argc - 1] = arg;
                args[argc] = 0;
                exec(args[0], args);
                exit(0);
            }
            else
            {
                wait(0);
                buf_index = 0;
            }
        }
        else
        {
            arg[buf_index++] = buf;
        }
    }

    exit(0);
}
