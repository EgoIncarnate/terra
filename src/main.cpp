#include <stdio.h>
#include <signal.h>
#include <getopt.h>
#include "terra.h"

static void doerror(lua_State * L) {
    printf("%s\n",luaL_checkstring(L,-1));
    exit(1);
}
const char * progname = NULL;
static void dotty (lua_State *L);
void parse_args(lua_State * L, int * argc, char *** argv, bool * interactive);
int main(int argc, char ** argv) {
    progname = argv[0];
    lua_State * L = luaL_newstate();
    luaL_openlibs(L);
    if(terra_init(L))
        doerror(L);
    bool interactive = false;
    
    parse_args(L,&argc,&argv,&interactive);
    
    // store the command line arguments in global 'arg'
    lua_newtable(L);
    for(int i = 0; i < argc; i++) {
      lua_pushnumber(L, i);
      lua_pushstring(L,argv[i]);
      lua_rawset(L, -3); 
    }
    lua_setglobal(L, "arg");

    if(argc > 0) {
      if(terra_dofile(L,argv[0]))
        doerror(L);
    }
    
    if(isatty(0) && (interactive || argc == 0)) {
        progname = NULL;
        dotty(L);
    }
    
    return 0;
}

static void print_welcome();
void usage() {
    print_welcome();
    printf("terra [OPTIONS] [source-files]\n"
           "    -v enable verbose debugging output\n"
           "    -h print this help message\n"
           "    -i enter the REPL after processing source files\n"
           "    -l <language_file> specify a module that defines a language extension (can be repeated)\n");
}

void parse_args(lua_State * L, int * argc, char *** argv, bool * interactive) {
    int ch;
    static struct option longopts[] = {
        { "help",      0,     NULL,           'h' },
        { "verbose",   0,     NULL,           'v' },
        { "interactive",     0,     NULL,     'i' },
        { "language", 1, NULL,                'l' },
        { NULL,        0,     NULL,            0 }
    };
    int verbose = 0;
    /*  Parse commandline options  */
    opterr = 0;
    while ((ch = getopt_long(*argc, *argv, "hvil:", longopts, NULL)) != -1) {
        switch (ch) {
            case 'v':
                verbose++;
                terra_setverbose(L,verbose);
                break;
            case 'i':
                *interactive = true;
                break;
            case 'l':
                if(terra_loadfile(L,optarg) || lua_pcall(L,0,1,0) || terra_loadlanguage(L))
                  doerror(L);
                break;
            case ':':
            case 'h':
            default:
                usage();
                exit(-1);
                break;
        }
    }
    *argc -= optind;
    *argv += optind;
}
//this stuff is from lua's lua.c repl implementation:

#include "linenoise.h"
#define lua_readline(L,b,p)    ((void)L, ((b)=linenoise(p)) != NULL)
#define lua_saveline(L,idx) \
    if (lua_strlen(L,idx) > 0)  /* non-empty line? */ \
      linenoiseHistoryAdd(lua_tostring(L, idx));  /* add it to history */
#define lua_freeline(L,b)    ((void)L, free(b))

static void l_message (const char *pname, const char *msg) {
  if (pname) fprintf(stderr, "%s: ", pname);
  fprintf(stderr, "%s\n", msg);
  fflush(stderr);
}

static int report (lua_State *L, int status) {
  if (status && !lua_isnil(L, -1)) {
    const char *msg = lua_tostring(L, -1);
    if (msg == NULL) msg = "(error object is not a string)";
    l_message(progname, msg);
    lua_pop(L, 1);
  }
  return status;
}

static int incomplete (lua_State *L, int status) {
  if (status == LUA_ERRSYNTAX) {
    size_t lmsg;
    const char *msg = lua_tolstring(L, -1, &lmsg);
    const char *tp = msg + lmsg - (sizeof("<eof>") - 1);
    if (strstr(msg, "<eof>") == tp) {
      lua_pop(L, 1);
      return 1;
    }
  }
  return 0;  /* else... */
}

#define LUA_MAXINPUT 512
#define LUA_PROMPT "> " 
#define LUA_PROMPT2 ">> "

static const char *get_prompt (lua_State *L, int firstline) {
  const char *p;
  lua_getfield(L, LUA_GLOBALSINDEX, firstline ? "_PROMPT" : "_PROMPT2");
  p = lua_tostring(L, -1);
  if (p == NULL) p = (firstline ? LUA_PROMPT : LUA_PROMPT2);
  lua_pop(L, 1);  /* remove global */
  return p;
}

static int pushline (lua_State *L, int firstline) {
  char buffer[LUA_MAXINPUT];
  char *b = buffer;
  size_t l;
  const char *prmt = get_prompt(L, firstline);
  if (lua_readline(L, b, prmt) == 0)
    return 0;  /* no input */
  l = strlen(b);
  if (l > 0 && b[l-1] == '\n')  /* line ends with newline? */
    b[l-1] = '\0';  /* remove it */
  if (firstline && b[0] == '=')  /* first line starts with `=' ? */
    lua_pushfstring(L, "return %s", b+1);  /* change it to `return' */
  else
    lua_pushstring(L, b);
  lua_freeline(L, b);
  return 1;
}


static int loadline (lua_State *L) {
  int status;
  lua_settop(L, 0);
  if (!pushline(L, 1))
    return -1;  /* no input */
  for (;;) {  /* repeat until gets a complete line */
    status = terra_loadbuffer(L, lua_tostring(L, 1), lua_strlen(L, 1), "stdin");
    if (!incomplete(L, status)) break;  /* cannot try to add lines? */
    if (!pushline(L, 0))  /* no more input? */
      return -1;
    lua_pushliteral(L, "\n");  /* add a new line... */
    lua_insert(L, -2);  /* ...between the two lines */
    lua_concat(L, 3);  /* join them */
  }
  lua_saveline(L, 1);
  lua_remove(L, 1);  /* remove line */
  return status;
}

static int traceback (lua_State *L) {
  if (!lua_isstring(L, 1))  /* 'message' not a string? */
    return 1;  /* keep it intact */
  lua_getfield(L, LUA_GLOBALSINDEX, "debug");
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return 1;
  }
  lua_getfield(L, -1, "traceback");
  if (!lua_isfunction(L, -1)) {
    lua_pop(L, 2);
    return 1;
  }
  lua_pushvalue(L, 1);  /* pass error message */
  lua_pushinteger(L, 2);  /* skip this function and traceback */
  lua_call(L, 2, 1);  /* call debug.traceback */
  return 1;
}


static lua_State *globalL = NULL;

static void lstop (lua_State *L, lua_Debug *ar) {
  (void)ar;  /* unused arg. */
  lua_sethook(L, NULL, 0, 0);
  luaL_error(L, "interrupted!");
}

static void laction (int i) {
  signal(i, SIG_DFL); /* if another SIGINT happens before lstop,
                              terminate process (default action) */
  lua_sethook(globalL, lstop, LUA_MASKCALL | LUA_MASKRET | LUA_MASKCOUNT, 1);
}


static int docall (lua_State *L, int narg, int clear) {
  int status;
  int base = lua_gettop(L) - narg;  /* function index */
  lua_pushcfunction(L, traceback);  /* push traceback function */
  lua_insert(L, base);  /* put it under chunk and args */
  signal(SIGINT, laction);
  status = lua_pcall(L, narg, (clear ? 0 : LUA_MULTRET), base);
  signal(SIGINT, SIG_DFL);
  lua_remove(L, base);  /* remove traceback function */
  /* force a complete garbage collection in case of errors */
  if (status != 0) lua_gc(L, LUA_GCCOLLECT, 0);
  return status;
}
static void print_welcome() {
    printf("\n"
           "Terra -- A low-level counterpart to Lua\n"
           "\n"
           "Stanford University\n"
           "zdevito@stanford.edu\n"
           "\n");
}
static void dotty (lua_State *L) {
  int status;
  globalL = L;
  print_welcome();
  while ((status = loadline(L)) != -1) {
    if (status == 0) status = docall(L, 0, 0);
    report(L,status);
    if (status == 0 && lua_gettop(L) > 0) {  /* any result to print? */
      lua_getglobal(L, "print");
      lua_insert(L, 1);
      if (lua_pcall(L, lua_gettop(L)-1, 0, 0) != 0)
        lua_pushfstring(L,
                        "error calling " LUA_QL("print") " (%s)",
                        lua_tostring(L, -1));
        report(L,status);
    }
  }
  lua_settop(L, 0);  /* clear stack */
  fputs("\n", stdout);
  fflush(stdout);
}

#if 0
//a much simpler main function:
#include <stdio.h>
#include "terra.h"

static void doerror(lua_State * L) {
    printf("%s\n",luaL_checkstring(L,-1));
    exit(1);
}
int main(int argc, char ** argv) {
    lua_State * L = luaL_newstate();
    luaL_openlibs(L);
    if(terra_init(L))
        doerror(L);
    for(int i = 1; i < argc; i++)
        if(terra_dofile(L,argv[i]))
            doerror(L);
    return 0;
}
#endif
