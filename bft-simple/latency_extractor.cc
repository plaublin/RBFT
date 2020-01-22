/*
 * The client saves, for each request, its latency and the view in which it has been executed in a binary file.
 * This program converts the binary file to a human-readable one.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include "lat_req.h"


int LATENCY_EXTRACTOR_MAIN(int argc, char **argv)
{
  if (argc < 2)
  {
    printf("Usage: %s <file1.dat> [<file2.dat> ...]\n", argv[0]);
    exit(0);
  }

  char filename[256];
  int i;
  for (i = 1; i < argc; i++)
  {
    printf("Processing %s ...", argv[i]);

    sprintf(filename, "%s.log", argv[i]);
    convert(argv[i], filename);

    printf("DONE\n");
  }

  return 0;
}
