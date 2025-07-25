#include "kernel/types.h"
#include "user/user.h"
#include "kernel/param.h"
#include "kernel/fcntl.h"

#define MAXBUFSIZ 256 
#define MAXSTACKSIZ 100 
#define MAXARGS 10
#define ECHO "echo"
#define GREP "grep"
#define CAT "cat"   
/* Not allowed to use _MLOC*/

/********************** definition *************************/
char whitespace[] = " \t\r\n\v";
char symbols[] = "<|>&;()";



typedef enum CmdType
{
  Execcmd,
  Redircmd,
  Pipecmd,
  NULLcmd
} cmdtype;

typedef enum RedirType
{
  Stdout2file,
  File2stdin
} redirtype;

typedef enum Boolean
{
  false,
  true
} boolean;


typedef struct cmd
{
  /* data */
  cmdtype type;
  union cmdcontent
  {
    struct pipecmd 
    {
      /* data */
      struct cmd* leftcmd;
      struct cmd* rightcmd;
    } pipecmd;

    struct redircmd
    {
      /* data */
      struct cmd* stdincmd;
      struct cmd* stdoutcmd;
      redirtype redirtype;  // <: file2stdin 或者 >: stdout2file 
      int fd; //相关的IO描述符
      char *file; //文件名
      int mode; //打开文件方式
    } redircmd;

    struct execcmd {
      char *argv[MAXARGS];
    } execcmd;

  } cmdcontent;
} cmd;


cmd cmdstack[MAXSTACKSIZ];     //用于给cmd分配空间，保存递归cmd
char tokens[MAXARGS][MAXPATH]; //用于保存execcmd的参数
char files[MAXARGS][MAXPATH];  //用于保存redircmd文件名
/**************************** headerfunction ***********************/
cmd* parsecmd(char *cmd, char *_endofcmd,int currentstackpointer);


/************************* utils ************************/
/* borrow from sh.c line 310 */
void init(); //清空cmdstack以及tokens
int gettoken(char **ps, char *es,int startpos, char **token, char **endoftoken); //利用空格切分Token
void parsetoken(char **token, char *endoftoken, char *parsedtoken);
int allocatestack();
int allocatetokens();
int allocatefiles();
/* do with cmd */
void evaluate(cmd* parsedcmd);
void preprocessCmd(char *cmd);

/*************************** code_implements *************************/

cmd nullcmd;

int 
main() { 
  char _cmd[MAXBUFSIZ];           // 存放用户输入的命令字符串
  nullcmd.type = NULLcmd;         // 初始化一个空命令，用作占位符

  while (true)                    // 主循环：不断读取并执行命令
  {
      memset(_cmd, 0, sizeof(_cmd));  // 清空命令缓冲区，避免残留数据
      printf("@ ");                   // 打印提示符，和题目要求一致
      gets(_cmd, MAXBUFSIZ);          // 从标准输入读取一行命令
      preprocessCmd(_cmd);            // 去掉结尾的换行符等处理

      if(strlen(_cmd) == 0 || _cmd[0] == 0){
          exit(0);                    // 如果输入为空，退出 shell
      }
      init();                         // 清空命令栈、参数数组等全局缓冲区
      char *endofcmd;                 
      endofcmd = _cmd + strlen(_cmd); // 指向命令字符串的末尾，用于解析
      // 递归解析命令行，返回命令树的根节点
      cmd* parsedcmd = parsecmd(_cmd, endofcmd, 0);
      // 创建子进程来执行命令树
      if(fork() == 0)
        evaluate(parsedcmd);          // 子进程执行命令（递归处理）     
      wait(0);                         // 父进程等待子进程执行结束
  }
  return 0;                         
}

/******************* 处理cmd ******************/
void 
evaluate(cmd *parsedcmd){
  int pd[2];

  if(parsedcmd->type == NULLcmd){
    return ;
  }
  
  switch (parsedcmd->type)
  { 
  /* code */
  case Pipecmd:

    pipe(pd);
    /* stdout */
    /* 左边命令的输出将会定位到标准输出内 */
    /* Child */
    if(fork() == 0){
      close(1);
      dup(pd[1]);
      close(pd[0]);
      close(pd[1]);
      evaluate(parsedcmd->cmdcontent.pipecmd.leftcmd);
    }
    /* stdin */
    /* 右边命令的输入将被重定向到标准输入内 */
    /* Parent */
    else {
      close(0);
      dup(pd[0]);
      close(pd[0]);
      close(pd[1]);
      evaluate(parsedcmd->cmdcontent.pipecmd.rightcmd);
    }
    wait(0);
    /* stdin */
    break;

  case Execcmd:
    exec(parsedcmd->cmdcontent.execcmd.argv[0], parsedcmd->cmdcontent.execcmd.argv);
    break;
  
  case Redircmd:
    close(parsedcmd->cmdcontent.redircmd.fd);
    if(open(parsedcmd->cmdcontent.redircmd.file, parsedcmd->cmdcontent.redircmd.mode) < 0){
      fprintf(2, "open %s failed\n", parsedcmd->cmdcontent.redircmd.file);
      exit(-1);
    }
    if(parsedcmd->cmdcontent.redircmd.redirtype == File2stdin){
      evaluate(parsedcmd->cmdcontent.redircmd.stdincmd);
    }
    else if(parsedcmd->cmdcontent.redircmd.redirtype == Stdout2file)
    {
      evaluate(parsedcmd->cmdcontent.redircmd.stdoutcmd);
    }
    break;
  default:
    break;
  }
  
}

/* | > < */
/* 管道|具有最高优先级 */
cmd* 
parsecmd(char *_cmd, char *_endofcmd, int currentstackpointer){
  char *s;
  s = _endofcmd;
  //printf("--------------------parsecmd--------------------\n");
  boolean isexec = true;
  boolean ispipe = false;
  /* 先找管道 */
  for(; s >= _cmd; s--){
    if (*s == '|')
    {
      cmdstack[currentstackpointer].type = Pipecmd;
      cmdstack[currentstackpointer].cmdcontent.pipecmd.leftcmd = parsecmd(_cmd, s - 1, allocatestack());
      cmdstack[currentstackpointer].cmdcontent.pipecmd.rightcmd = parsecmd(s + 1, _endofcmd, allocatestack());
      isexec = false;
      ispipe = true;
      break;
    }
  }
  /* 再找重定向符 */
  if(!ispipe){
    s = _endofcmd;
    for (; s >= _cmd; s--)
    {
      /* code */
      if (*s == '<' || *s == '>')
      {
        cmdstack[currentstackpointer].type = Redircmd;
        /* code */
        /* stdin < file */
        if(*s == '<'){
          cmdstack[currentstackpointer].cmdcontent.redircmd.redirtype = File2stdin;
          cmdstack[currentstackpointer].cmdcontent.redircmd.fd = 0;
          cmdstack[currentstackpointer].cmdcontent.redircmd.stdincmd = parsecmd(_cmd, s - 1, allocatestack());
          cmdstack[currentstackpointer].cmdcontent.redircmd.stdoutcmd = &nullcmd;
          cmdstack[currentstackpointer].cmdcontent.redircmd.mode = O_RDONLY;
        }
        /* stdout > file */
        else {
          cmdstack[currentstackpointer].cmdcontent.redircmd.redirtype = Stdout2file;
          cmdstack[currentstackpointer].cmdcontent.redircmd.fd = 1;
          cmdstack[currentstackpointer].cmdcontent.redircmd.stdincmd = &nullcmd;
          cmdstack[currentstackpointer].cmdcontent.redircmd.stdoutcmd = parsecmd(_cmd, s - 1, allocatestack());
          cmdstack[currentstackpointer].cmdcontent.redircmd.mode = O_WRONLY|O_CREATE;
        }
        char *file, *endoffile;
        gettoken(&_cmd, _endofcmd,  s - _cmd + 1, &file, &endoffile);
        int pos = allocatefiles();
        parsetoken(&file, endoffile, files[pos]);
        cmdstack[currentstackpointer].cmdcontent.redircmd.file = files[pos]; 
        isexec = false;
        break;
      }
    }
    if(isexec){
      cmdstack[currentstackpointer].type = Execcmd;
      int totallen = _endofcmd - _cmd;
      int startpos = 0;
      int count = 0;
      while (startpos < totallen)
      {
        /* code */
        char *token, *endoftoken;
        startpos = gettoken(&_cmd, _endofcmd, startpos, &token, &endoftoken);
        if(*token != ' '){
          int pos = allocatetokens();
          parsetoken(&token, endoftoken, tokens[pos]);
          cmdstack[currentstackpointer].cmdcontent.execcmd.argv[count] = tokens[pos];
          count++;
        }
      }
      cmdstack[currentstackpointer].cmdcontent.execcmd.argv[count] = 0;
    }
  }
  return &cmdstack[currentstackpointer];
}

void 
init(){
  memset(tokens, 0, sizeof(tokens));
  memset(files, 0, sizeof(files));
  memset(cmdstack, 0, sizeof(cmdstack));
  for (int i = 0; i < MAXSTACKSIZ; i++)
  {
    /* code */
    cmdstack[i].type = NULLcmd;
  }
}
/* 分配栈 */
int
allocatestack(){
  int newpointer = 0;
  while(cmdstack[newpointer].type != NULLcmd) newpointer++;
  return newpointer;
}

int
allocatetokens(){
  int newpointer = 0;
  while(tokens[newpointer][0] != 0) newpointer++;
  return newpointer;
}

int
allocatefiles(){
  int newpointer = 0;
  while(files[newpointer][0] != 0) newpointer++;
  return newpointer;
}

/* 去掉回车符 */
void
preprocessCmd(char *cmd){
  int n = strlen(cmd);
  if(n > MAXBUFSIZ){
      printf("command too long!");
      exit(0);
  }
  else
  {
      /* code */
      if(cmd[n - 1] == '\n'){
          cmd[n - 1] = '\0';
      }
  }
}
  
/************************* Utils **************************/
void parsetoken(char **token, char *endoftoken, char *parsedtoken){
  //printf("gettoken: ");
  char *s = *token;
  for (; s < endoftoken; s++)
  {
    *(parsedtoken++) = *s;
    //printf("%c", *s);
  }
  *parsedtoken = '\0';
  //printf("\n");
}

int
gettoken(char **ps, char *es, int startpos, char **token, char **endoftoken)
{
  char *s;
  int pos = startpos;
  s = *ps + startpos;
  /* 清理所有s的空格 trim */
  while(s < es && strchr(whitespace, *s)){
    s++;
    pos++;
  }
  *token = s;
  while (s < es && !strchr(whitespace, *s))
  {
    /* code */
    s++;
    pos++;
  }
  *endoftoken = s;
  return pos;
}  