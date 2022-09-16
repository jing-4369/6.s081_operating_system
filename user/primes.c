#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define maxNum 35

void sieveprime(int *left)
{
    close(left[1]);
    int pid, prime, temp, right[2];

    if (read(left[0], &prime, sizeof(int)) == 0)
    {
        close(left[0]);
        exit(0);
    }
    pipe(right);
    printf("prime %d\n", prime);

    if ((pid = fork()) > 0)
    {
        close(right[0]);
        while (read(left[0], &temp, sizeof(int)))
        {
            if (temp % prime == 0)
            {
                continue;
            }
            write(right[1], &temp, sizeof(int));
        }
        close(left[0]);
        close(right[1]);
        wait(0);

    }
    else
    {
        sieveprime(right);

    }
}

int main(int argc, char **argv)
{
    int pid, fd[2];
    pipe(fd);

    if ((pid = fork()) > 0)
    {
        close(fd[0]);
        for (int i = 2; i <= maxNum; ++i)
        {
            write(fd[1], &i, sizeof(int));
        }
        close(fd[1]);
        wait(0);
    }
    else
    {
        sieveprime(fd);
    }
    exit(0);
}