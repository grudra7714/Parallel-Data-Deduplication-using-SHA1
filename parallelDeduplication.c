#include "headers.h"
#include "parallelDeduplication.h"
#include "defines.h"
#include "structs.h"
#include "functions.h"

int main(int argc, char *argv[])
{
   double         clock;
   struct buffer  buf;
   char          *pattern = NULL;
   char          *inputFile  = NULL;
   char          *outdir = strdup(".");
 double starttime, endtime; 
 
   parse_commandline(argc, argv, &pattern, &inputFile, &outdir);
   if(inputFile == NULL)
      return EXIT_FAILURE;
 
   MPI_Init(&argc, &argv);
   starttime = MPI_Wtime();

   tic(&clock);

	int i = 0;
  	 
  

  load_file(inputFile, &buf);
   toc(&clock, "Read input:");

   transfer_partials(pattern, &buf);
   toc(&clock, "Transfer:");

   write_chunks(inputFile, outdir, &buf);
   toc(&clock, "Write chunks:");

  endtime   = MPI_Wtime(); 
   //printf("\nThat took %f seconds\n",endtime-starttime); 
   MPI_Finalize();
   free(buf.data);
   free(pattern);
   free(inputFile);
 
  return 0;
}


int file_exists(const char *path)
{
   return access(path, R_OK) == 0;
}

long getfilesize(const char *path)
{
  struct stat buf;
  if(stat(path, &buf) == -1)
    return -1;
  return buf.st_size;
}

//a191b34ab37fe0bb20e7f4a257a000226e7429930c762c719b47b65eb230c0b1
//48 c9 8f 7e 5a 6e 73 6d 79 0a b7 40dfc3f51a61abe2b5


int parse_commandline(int argc, char **argv,
                      char **pattern,
                      char **inputFile,
                      char **outdir)
{
   while(--argc) {
      ++argv;
  //printf("\n%s", argv[0]);
      if(streq(argv[0], "-r")) {
         *pattern = strdup(argv[1]);
    
         --argc;
         ++argv;
      }
      else if(streq(argv[0], "-o")) {
         free(*outdir);
         *outdir = strdup(argv[1]);
         --argc;
         ++argv;
      }
      else if(startswith(argv[0], "-r")) {
         *pattern = strdup(argv[0] + 2);
      }
      else if(startswith(argv[0], "--regex=")) {
         *pattern = strdup(argv[0] + 8);
      }
      else if(startswith(argv[0], "--")) {
         predefine_t *p = &PREDEFINE_LIST[0];
         for(; p->name != NULL; ++p) {
            if(streq(p->name, argv[0])) {
               *pattern = strdup(p->pattern);
            }
         }
      }
      else {
         if(file_exists(argv[0])) {
            *inputFile = strdup(argv[0]);
         }
      }
   }
}
void advance_record(const char *pattern, struct buffer *buf)
{
   pcre       *regex = NULL;
   const char *err;
   int         erroffset;
   int         rank;
   int         retcode;
   int         matches[3] = { 0 };

   MPI_Comm_rank(MPI_COMM_WORLD, &rank);

   regex = pcre_compile(pattern, PCRE_MULTILINE, &err, &erroffset, NULL);
   if(regex == NULL) {
      fprintf(stderr, "An error occured while compiling regular expression, "
                      "%s at offset %d.\n", err, erroffset);
      MPI_Abort(MPI_COMM_WORLD, 1);
   }
   
   retcode = pcre_exec(regex,
                       NULL,
                       buf->start,
                       buf->end - buf->start,
                       0, 0, matches, 3);
   if(retcode < 0) {
      if(retcode == PCRE_ERROR_NOMATCH) {
         fprintf(stderr, "No record header found in chunk %d "
                         "matching regular expression %s.\n", 
                         rank, pattern);
      }
      else {
         fprintf(stderr, "An unknown error occured in chunk %d.\n", rank);
      }
      MPI_Abort(MPI_COMM_WORLD, 1);
   }
   buf->start += matches[0];
   pcre_free(regex);
}

void load_file(char *filename, struct buffer *buf)
{
   MPI_File fh;
   MPI_Status status;
   int    size, rank;
   long   filesize, chunksize;
   int    count;
   int    rescode;

   MPI_Comm_size(MPI_COMM_WORLD, &size);
   MPI_Comm_rank(MPI_COMM_WORLD, &rank);

   filesize  = getfilesize(filename);
   chunksize = (long) ceil((double)filesize / size);

   if(rank == 0) {
      printf("File size:    %12ld bytes\n", filesize);
      printf("Chunk size:   %12ld bytes\n", chunksize);
      printf("Num chunks:   %12d\n", size);
   }

   rescode = MPI_File_open(MPI_COMM_WORLD,
                           filename,
                           MPI_MODE_RDONLY,
                           MPI_INFO_NULL,
                           &fh);
   if(rescode == MPI_ERR_IO) {
      fprintf(stderr,
              "Process #%d cannot open %s for reading.\n",
              rank, filename);
      MPI_Abort(MPI_COMM_WORLD, 1);
   }
   buf->data = malloc(chunksize + MAX_PARTIAL_SIZE);
   MPI_File_set_view(fh,
                     rank * chunksize,
                     MPI_CHAR, MPI_CHAR,
                     "native", MPI_INFO_NULL);
   MPI_File_read(fh, buf->data, chunksize, MPI_CHAR, &status);
   MPI_Get_count(&status, MPI_CHAR, &count);
   MPI_File_close(&fh);

   buf->start = buf->data;
   buf->end   = buf->data + count;
}


void transfer_partials(const char *pattern, struct buffer *buf)
{
   int size, rank;
   MPI_Comm_size(MPI_COMM_WORLD, &size);
   MPI_Comm_rank(MPI_COMM_WORLD, &rank);

   if(rank > 0) {
      if(pattern != NULL)
         advance_record(pattern, buf);
      MPI_Send(buf->data,
               buf->start - buf->data,
               MPI_CHAR,
               rank - 1, TAG_PARTIAL, MPI_COMM_WORLD);
   }
   if(rank < size - 1) {
      MPI_Status stat;
      int count;

      MPI_Recv(buf->end, MAX_PARTIAL_SIZE, MPI_CHAR,
               rank + 1, TAG_PARTIAL, MPI_COMM_WORLD, &stat);
      MPI_Get_count(&stat, MPI_CHAR, &count);
      buf->end += count;
   }
}


void write_chunks(char *filename, char *outdir, struct buffer *buf)
{
   int rank;
   char outfilename[MAX_PATH_LENGTH];
   char *filebase;
   FILE *fh;
   unsigned char result[SHA_DIGEST_LENGTH];
   const char *string ;
   int i = 0;

   
   filebase = strdup(filename);
   filebase = basename(filebase);
   MPI_Comm_rank(MPI_COMM_WORLD, &rank);
   sprintf(outfilename, "%s/%s.%03d", outdir, filebase, rank);
   fh = fopen(outfilename, "w");
   fwrite(buf->start, 1, buf->end - buf->start, fh);
   fclose(fh);



  char * buffer = 0;
  long length;

  FILE *f = fopen (outfilename, "rb");

  //function to encrypt content of the data using SHA2-hash

  int size = ftell(f);
      do_fp(f);
/*    if(size != 0){
      do_fp(f);
    }
    else
      printf("\nFile not empty\n");
*/
/*  if (f)
  {
    fseek (f, 0, SEEK_END);
    length = ftell (f);
    fseek (f, 0, SEEK_SET);
    buffer = malloc (length);
    if (buffer)
    {
      fread (buffer, 1, length, f);
    }
    fclose (f);
  }

  if (buffer)
  {
    // start to process your data / extract strings here...
 
  SHA1(buffer, strlen(buffer), result);
  printf("%s", result);
*/ 
/*  for(i = 0; i < SHA_DIGEST_LENGTH; i++)
    printf("%02x%c", result[i], i < (SHA_DIGEST_LENGTH-1) ? ' ' : '\n');

  }
*/
}

void do_fp(FILE *f)
{
    SHA_CTX c;
    unsigned char md[SHA_DIGEST_LENGTH];
    int fd;
    int i;
    unsigned char buf[BUFSIZE];

    fd = fileno(f);
    SHA1_Init(&c);
    for (;;) {
        i = read(fd, buf, BUFSIZE);
        if (i <= 0)
            break;
        SHA1_Update(&c, buf, (unsigned long)i);
    }
    SHA1_Final(&(md[0]), &c);
    pt(md);
}
void pt(unsigned char *md)
{
    int i;

   char src[50], dest[50];
   char estr[150];
/*   strcpy(src,  "");
   strcpy(dest, "");

   strcat(dest, src);
*/
    FILE *f = fopen("encryptedLog.txt", "a");
    for (i = 0; i < SHA_DIGEST_LENGTH; i++){
        sprintf(&estr[i], "%02x", md[i]);
    }

    int flag = check(estr);
    if(flag != 0){
      //fprintf(f1, "");
      fprintf(f, estr);
      fprintf(f, "\n");
    }
    else{
      puts("Possible Duplicate");
      exit(0);
      //MPI_Abort(MPI_COMM_WORLD, 1);
    }
    //puts(estr);
    fclose(f);
}

int check(char *estr){
     FILE * fp;
     char * line = NULL;
     size_t len = 0;
     ssize_t read;

     fp = fopen("encryptedLog.txt", "r");
     if (fp == NULL)
         exit(EXIT_FAILURE);

     while ((read = getline(&line, &len, fp)) != -1) {
         //printf("Retrieved line of length %zu :\n", read);
        //printf("%s", line);
       strip(line);
       //puts(line);
        if (strcmp(estr,line) == 0){
          printf("Possible Duplicate");
          return 0;
        }
     }
}

void strip(char *s) {
    char *p2 = s;
    while(*s != '\0') {
      if(*s != '\t' && *s != '\n') {
        *p2++ = *s++;
      } else {
        ++s;
      }
    }
    *p2 = '\0';
}
