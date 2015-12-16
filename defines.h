#define MAX_PATH_LENGTH  (4096)
#define MAX_PARTIAL_SIZE (1024)
#define TAG_PARTIAL      (0)

#define streq(X,Y)      (strcmp((X),(Y))==0)
#define startswith(X,P) (strncmp((X), (P), strlen(P)) == 0)
#define BUFSIZE 1024*16
