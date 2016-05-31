/**
 * \file
 *         shell.c - Contiki BBS core shell based on the Contiki OS shell. 
 *
 *         Contiki OS Shell Copyright (c) 2008, Swedish Institute of Computer Science.
 *         All rights reserved.
 *
 * \author
 *         Contiki BBS shell modifications (c) 2009-2015 by Niels Haedecke <n.haedecke@unitybox.de>
 */

#include "contiki.h"
#include "contiki-lib.h"

#include "shell.h"

#include <ctype.h>
#include <string.h>
#include <stdio.h>

LIST(commands);

int shell_event_input;
static struct process *front_process;
static unsigned long time_offset;
/*static struct etimer bbs_login_timer;*/
 
BBS_STATUS_REC bbs_status;
BBS_USER_REC bbs_user;
unsigned short bbs_locked=0;

/*---------------------------------------------------------------------------*/
PROCESS(shell_process, "Shell");
PROCESS(shell_server_process, "Shell server");
PROCESS(bbs_version_process, "version");
SHELL_COMMAND(bbs_version_command, "version", "version: show version and copyright", 
              &bbs_version_process);
PROCESS(help_command_process, "help");
SHELL_COMMAND(help_command, "help", "help   : shows this help",
	      &help_command_process);
SHELL_COMMAND(question_command, "?", "?      : shows this help",
	      &help_command_process);
PROCESS(shell_killall_process, "killall");
SHELL_COMMAND(killall_command, "killall", "killall : stop all running commands",
	      &shell_killall_process);
PROCESS(shell_kill_process, "kill");
SHELL_COMMAND(kill_command, "kill", "kill <command> : stop a specific command",
	      &shell_kill_process);
PROCESS(shell_exit_process, "exit");
SHELL_COMMAND(exit_command, "exit", "exit   : exit bbs",
	      &shell_exit_process);
SHELL_COMMAND(quit_command, "quit", "quit   : exit bbs",
	      &shell_exit_process);
PROCESS(bbs_login_process, "login");
SHELL_COMMAND(bbs_login_command, "login", "login  : login proc", &bbs_login_process);
PROCESS(bbs_timer_process, "timer");
/*---------------------------------------------------------------------------*/
short bbs_filesize(char *filename)
{
    struct cbm_dirent dirent;
    unsigned short fsize=0;

    if (cbm_opendir(1,bbs_status.board_drive)==0) {
        while (!cbm_readdir(1, &dirent))
            if (strstr(dirent.name, filename)) 
               fsize=dirent.size;
        cbm_closedir(1);
    }
    return fsize*256; /* one block is 256 bytes */
}
/*---------------------------------------------------------------------------*/
static void bbs_init(void) 
{
   ST_FILE file;

  /* read BBS base configuration */
   strcpy(file.szFileName, BBS_CFG_FILE);  
   file.ucDeviceNo = 8;
   ssReadRELFile(&file, &bbs_status, sizeof(bbs_status), 1);

   /* set BBS parameters */
   bbs_status.bbs_board_id=1;
   bbs_status.bbs_msg_id=1;
}
/*---------------------------------------------------------------------------*/
void bbs_banner(unsigned char szBannerFile[15]) 
{
  unsigned char *buffer;
  unsigned short fsize=0;
  unsigned short i=0, siRet=0, len=0;

  fsize=bbs_filesize(szBannerFile);

  if (fsize == 0) {
    shell_output_str(NULL, "", "error: file size\n\r");
    return;
  }

  buffer = (char*) malloc(fsize);

  if (buffer == NULL) {
    shell_output_str(NULL, "", "error: malloc \n\r");
    return;
  }

  memset(buffer, 0, fsize);
  siRet = cbm_open(10, bbs_status.board_drive, 10, szBannerFile);

  if (! siRet) {
     len = cbm_read(10, buffer, fsize);
     cbm_close(10);

     for (i=0; i<len; i++) {
         if (buffer[i] == '\r')
            buffer[i] = '\n';
     }
  }
 
  shell_output_str(NULL, "", buffer);
  
  if (buffer != NULL)
     free(buffer);
}
/*---------------------------------------------------------------------------*/
void bbs_splash(unsigned short mode) 
{
  if (mode==BBS_MODE_CONSOLE)
    log_message("",BBS_COPYRIGHT_STRING);
  else
    shell_output_str(&bbs_version_command,"",BBS_COPYRIGHT_STRING);
}
/*---------------------------------------------------------------------------*/
void bbs_lock(void)
{
  bbs_status.bbs_board_id=1;
  bbs_status.bbs_msg_id=1;
  process_start(&bbs_timer_process, NULL);
}
/*---------------------------------------------------------------------------*/
void bbs_unlock(void)
{
  log_message("[bbs] ", "*session timeout*");

  bbs_locked=0;
  bbs_status.bbs_status=0;
  bbs_status.bbs_board_id=1;
  bbs_status.bbs_msg_id=1;
  process_exit(&bbs_timer_process);
  shell_exit();
}
/*---------------------------------------------------------------------------*/
int bbs_get_user(char *data)
{
  int user_count,count=1;
  ST_FILE file;

  strcpy(file.szFileName, "user.idx");
  file.ucDeviceNo = bbs_status.board_drive;
  ssReadRELFile(&file, &user_count, sizeof(unsigned short), 1);

  strcpy(file.szFileName, "user.dat");
  file.ucDeviceNo = bbs_status.board_drive;

  do {     
       memset(&bbs_user, 0, sizeof(BBS_USER_REC));
       ssReadRELFile(&file, &bbs_user, sizeof(BBS_USER_REC), count);

       if (! strcmp(bbs_user.user_name, data)) {
          return count;
       }

       count++;
  } while (count <= user_count); 

  return 0;
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(bbs_login_process, ev, data)
{
  struct shell_input *input;

  PROCESS_BEGIN();

  while(1) {

    PROCESS_WAIT_EVENT_UNTIL(ev == shell_event_input || ev == PROCESS_EVENT_TIMER);

    if (ev == PROCESS_EVENT_TIMER)
       bbs_unlock(); 

    if (ev == shell_event_input) {
       input = data;

       switch (bbs_status.bbs_status) {

         case 0: {
                    if ((bbs_get_user(input->data1) != 0)) {
                       shell_prompt("password: ");
                       bbs_status.bbs_status=1;
                    } else {
                       shell_output_str(&bbs_login_command, "login failed.", "");
                       bbs_unlock();
                    }
                    break;
         }

         case 1: {
                    if(! strcmp(input->data1, bbs_user.user_pwd)) {
                       process_exit(&bbs_timer_process);
                       bbs_status.bbs_status=2;
                       log_message("[bbs] *login* ", bbs_user.user_name);  
                       bbs_banner(BBS_BANNER_MENU);
                       shell_prompt(bbs_status.bbs_prompt);
                       process_start(&bbs_timer_process, NULL);
                       front_process=&shell_process;
                    } else {
                       shell_output_str(&bbs_login_command, "login failed.", "");
                       bbs_unlock();
                    }
                    break;
         }
       }
    }
  } /* end ... while */

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
static void
command_kill(struct shell_command *c)
{
  if(c != NULL) {
    shell_output_str(&killall_command, "Stopping command ", c->command);
    process_exit(c->process);
  }
}
/*---------------------------------------------------------------------------*/
static void
killall(void)
{
  struct shell_command *c;
  for(c = list_head(commands);
      c != NULL;
      c = c->next) {
    if(c != &killall_command && process_is_running(c->process)) {
      command_kill(c);
    }
  }
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(bbs_timer_process, ev, data)
{
  static struct etimer bbs_session_timer;
  char szBuff[20];

  PROCESS_BEGIN();

  if (bbs_status.bbs_status==2)
     etimer_set(&bbs_session_timer, CLOCK_SECOND * bbs_status.bbs_timeout_session);
  else
     etimer_set(&bbs_session_timer, CLOCK_SECOND * bbs_status.bbs_timeout_login);

  while (1) {

     PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&bbs_session_timer));

     if (ev == PROCESS_EVENT_TIMER) {
        if (bbs_status.bbs_status==2)
           process_post(PROCESS_BROADCAST, PROCESS_EVENT_TIMER, NULL);
        else
           process_post(&bbs_login_process, PROCESS_EVENT_TIMER, NULL);

        sprintf(szBuff, "session timeout.");
        shell_output_str(NULL, szBuff, "");
        if (bbs_status.bbs_status==2)
           etimer_set(&bbs_session_timer, CLOCK_SECOND * bbs_status.bbs_timeout_session);
        else
           etimer_set(&bbs_session_timer, CLOCK_SECOND * bbs_status.bbs_timeout_login);
     } else {
       if (ev == shell_event_input) 
         if (bbs_status.bbs_status==2)
            etimer_set(&bbs_session_timer, CLOCK_SECOND * bbs_status.bbs_timeout_session);
         else
            etimer_set(&bbs_session_timer, CLOCK_SECOND * bbs_status.bbs_timeout_login);
     }
  }

  PROCESS_END();
}

/*---------------------------------------------------------------------------*/
PROCESS_THREAD(shell_killall_process, ev, data)
{

  PROCESS_BEGIN();

  killall();
  
  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(shell_kill_process, ev, data)
{
  struct shell_command *c;
  char *name;
  PROCESS_BEGIN();

  name = data;
  if(name == NULL || strlen(name) == 0) {
    shell_output_str(&kill_command,
		     "kill <command>: command name must be given", "");
  }

  for(c = list_head(commands);
      c != NULL;
      c = c->next) {
    if(strcmp(name, c->command) == 0 &&
       c != &kill_command &&
       process_is_running(c->process)) {
      command_kill(c);
      PROCESS_EXIT();
    }
  }

  shell_output_str(&kill_command, "Command not found: ", name);
  
  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(bbs_version_process, ev, data)
{
  PROCESS_BEGIN();

    bbs_splash(BBS_MODE_SHELL);

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(help_command_process, ev, data)
{
  struct shell_command *c;
  PROCESS_BEGIN();

  shell_output_str(&help_command, "Available commands:", "");
  for(c = list_head(commands);
      c != NULL;
      c = c->next) {
    shell_output_str(&help_command, c->description, "");
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(shell_exit_process, ev, data)
{
  PROCESS_BEGIN();

  bbs_banner(BBS_BANNER_LOGOUT);

  log_message("[bbs] *logut* ", bbs_user.user_name);  
  bbs_status.bbs_status=0;
  bbs_locked=0;
  shell_exit();

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
static void
replace_braces(char *commandline)
{
  char *ptr;
  int level = 0;
  
  for(ptr = commandline; *ptr != 0; ++ptr) {
    if(*ptr == '{') {
      if(level == 0) {
	*ptr = ' ';
      }
      ++level;
    } else if(*ptr == '}') {
      --level;
      if(level == 0) {
	*ptr = ' ';
      }
    }
  }
}
/*---------------------------------------------------------------------------*/
static char *
find_pipe(char *commandline)
{
  char *ptr;
  int level = 0;
  
  for(ptr = commandline; *ptr != 0; ++ptr) {
    if(*ptr == '{') {
      ++level;
    } else if(*ptr == '}') {
      --level;
    } else if(*ptr == '|') {
      if(level == 0) {
	return ptr;
      }
    }
  }
  return NULL;
}
/*---------------------------------------------------------------------------*/
static struct shell_command *
start_command(char *commandline, struct shell_command *child)
{
  char *next, *args;
  int command_len;
  struct shell_command *c;

  /* Shave off any leading spaces. */
  while(*commandline == ' ') {
    commandline++;
  }

  /* Find the next command in a pipeline and start it. */
  next = find_pipe(commandline);
  if(next != NULL) {
    *next = 0;
    child = start_command(next + 1, child);
  }

  /* Separate the command arguments, and remove braces. */
  replace_braces(commandline);
  args = strchr(commandline, ' ');
  if(args != NULL) {
    args++;
  }

  /* Shave off any trailing spaces. */
  command_len = (int)strlen(commandline);
  while(command_len > 0 && commandline[command_len - 1] == ' ') {
    commandline[command_len - 1] = 0;
    command_len--;
  }
  
  if(args == NULL) {
    command_len = (int)strlen(commandline);
    args = &commandline[command_len];
  } else {
    command_len = (int)(args - commandline - 1);
  }
  

  
  /* Go through list of commands to find a match for the first word in
     the command line. */
  for(c = list_head(commands);
      c != NULL &&
	!(strncmp(c->command, commandline, command_len) == 0 &&
	  c->command[command_len] == 0);
      c = c->next);
  
  if(c == NULL) {
    shell_output_str(NULL, commandline, ": command not found (try 'help')");
    command_kill(child);
    c = NULL;
  } else if(process_is_running(c->process) || child == c) {
    shell_output_str(NULL, commandline, ": command already running");
    c->child = NULL;
    c = NULL;
  } else {
    c->child = child;
    /*    printf("shell: start_command starting '%s'\n", c->process->name);*/
    /* Start a new process for the command. */
    process_start(c->process, args);
  }
  
  return c;
}
/*---------------------------------------------------------------------------*/
int
shell_start_command(char *commandline, int commandline_len,
		    struct shell_command *child,
		    struct process **started_process)
{
  struct shell_command *c;
  int background = 0;

  if(commandline_len == 0) {
    if(started_process != NULL) {
      *started_process = NULL;
    }
    return SHELL_NOTHING;
  }

  if(commandline[commandline_len - 1] == '&') {
    commandline[commandline_len - 1] = 0;
    background = 1;
    commandline_len--;
  }

  c = start_command(commandline, child);

  /* Return a pointer to the started process, so that the caller can
     wait for the process to complete. */
  if(c != NULL && started_process != NULL) {
    *started_process = c->process;
    if(background) {
      return SHELL_BACKGROUND;
    } else {
      return SHELL_FOREGROUND;
    }
  }
  return SHELL_NOTHING;
}
/*---------------------------------------------------------------------------*/
static void
input_to_child_command(struct shell_command *c,
		       char *data1, int len1,
		       const char *data2, int len2)
{
  struct shell_input input;
  if(process_is_running(c->process)) {
    input.data1 = data1;
    input.len1 = len1;
    input.data2 = data2;
    input.len2 = len2;
    process_post_synch(c->process, shell_event_input, &input);
  }
}
/*---------------------------------------------------------------------------*/
void
shell_input(char *commandline, int commandline_len)
{
  struct shell_input input;

  /*  printf("shell_input front_process '%s'\n", front_process->name);*/

  if(commandline[0] == '~' &&
     commandline[1] == 'K') {
    /*    process_start(&shell_killall_process, commandline);*/
    if(front_process != &shell_process) {
      process_exit(front_process);
    }
  } else {
    if(process_is_running(front_process)) {
      input.data1 = commandline;
      input.len1 = commandline_len;
      input.data2 = "";
      input.len2 = 0;
      process_post_synch(front_process, shell_event_input, &input);
    }
  }
}
/*---------------------------------------------------------------------------*/
void
shell_output_str(struct shell_command *c, char *text1, const char *text2)
{
  if(c != NULL && c->child != NULL) {
    input_to_child_command(c->child, text1, (int)strlen(text1),
			   text2, (int)strlen(text2));
  } else {
    shell_default_output(text1, (int)strlen(text1),
			 text2, (int)strlen(text2));
  }
}
/*---------------------------------------------------------------------------*/
void
shell_output(struct shell_command *c,
	     void *data1, int len1,
	     const void *data2, int len2)
{
  if(c != NULL && c->child != NULL) {
    input_to_child_command(c->child, data1, len1, data2, len2);
  } else {
    shell_default_output(data1, len1, data2, len2);
  }
}
/*---------------------------------------------------------------------------*/
void
shell_unregister_command(struct shell_command *c)
{
  list_remove(commands, c);
}
/*---------------------------------------------------------------------------*/
void
shell_register_command(struct shell_command *c)
{
  struct shell_command *i, *p;

  p = NULL;
  for(i = list_head(commands);
      i != NULL &&
	strcmp(i->command, c->command) < 0;
      i = i->next) {
    p = i;
  }
  if(p == NULL) {
    list_push(commands, c);
  } else if(i == NULL) {
    list_add(commands, c);
  } else {
    list_insert(commands, p, c);
  }
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(shell_process, ev, data)
{
  static struct process *started_process;
  /*static struct etimer bbs_session_timer;*/
  struct shell_input *input;
  int ret;

  PROCESS_BEGIN();

  /* Let the system start up before showing the prompt. */
  PROCESS_PAUSE();
  
  /*etimer_set(&bbs_session_timer, CLOCK_SECOND * bbs_status.bbs_timeout_session);*/

  while(1) {
  
    PROCESS_WAIT_EVENT();

    if (ev == shell_event_input)
    {
      input = data;
      /*etimer_reset(&bbs_session_timer);*/
      ret = shell_start_command(input->data1, input->len1, NULL,
				&started_process);
      if(started_process != NULL &&
	 ret == SHELL_FOREGROUND &&
	 process_is_running(started_process)) {
	front_process = started_process;
	PROCESS_WAIT_EVENT_UNTIL(ev == PROCESS_EVENT_EXITED &&
				 data == started_process);
      }
      front_process = &shell_process;
    }

    if (ev == PROCESS_EVENT_TIMER)
       bbs_unlock();

    if(bbs_status.bbs_status == 2) {
      /*etimer_set(&bbs_session_timer, CLOCK_SECOND * bbs_status.bbs_timeout_session);*/
      shell_prompt(bbs_status.bbs_prompt);
    }

  }
  
  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(shell_server_process, ev, data)
{
  struct process *p;
  struct shell_command *c;
  static struct etimer session_timer;
  PROCESS_BEGIN();

  etimer_set(&session_timer, CLOCK_SECOND * 10);
  while(1) {
    PROCESS_WAIT_EVENT();
    if(ev == PROCESS_EVENT_EXITED) {
      p = data;
      /*      printf("process exited '%s' (front '%s')\n", p->name,
	      front_process->name);*/
      for(c = list_head(commands);
	  c != NULL && c->process != p;
	  c = c->next);
      while(c != NULL) {
	if(c->child != NULL && c->child->process != NULL) {
	  /*	  printf("Killing '%s'\n", c->process->name);*/
	  input_to_child_command(c->child, "", 0, "", 0);
	  /*	  process_exit(c->process);*/
	}
	c = c->child;
      }
    } else if(ev == PROCESS_EVENT_TIMER) {
      etimer_reset(&session_timer);
      shell_set_time(shell_time());
    }
  }
  
  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
void
shell_init(void)
{
  /* register BBS processes */
  list_init(commands);
  shell_register_command(&help_command);
  shell_register_command(&question_command);
  shell_register_command(&exit_command);
  shell_register_command(&quit_command);
  shell_register_command(&bbs_version_command);

  /* local console eye candy */
  clrscr();
  /*bordercolor(0);
  bgcolor(0);
  textcolor(5);*/
  bbs_splash(BBS_MODE_CONSOLE);

  bbs_init();
  bbs_page_init(); 
  bbs_blist_init(); 
  bbs_read_init();
  bbs_setboard_init();
  bbs_post_init();

  shell_event_input = process_alloc_event();
  
  process_start(&bbs_login_process, NULL);
  process_start(&shell_process, NULL);
  process_start(&shell_server_process, NULL);

  front_process = &bbs_login_process;

  bbs_status.bbs_status=0;
}
/*---------------------------------------------------------------------------*/
unsigned long
shell_strtolong(const char *str, const char **retstr)
{
  int i;
  unsigned long num = 0;
  const char *strptr = str;

  if(str == NULL) {
    return 0;
  }
  
  while(*strptr == ' ') {
    ++strptr;
  }
  
  for(i = 0; i < 10 && isdigit(strptr[i]); ++i) {
    num = num * 10 + strptr[i] - '0';
  }
  if(retstr != NULL) {
    if(i == 0) {
      *retstr = str;
    } else {
      *retstr = strptr + i;
    }
  }
  
  return num;
}
/*---------------------------------------------------------------------------*/
unsigned long
shell_time(void)
{
  return clock_seconds() + time_offset;
}
/*---------------------------------------------------------------------------*/
void
shell_set_time(unsigned long seconds)
{
  time_offset = seconds - clock_seconds();
}
/*---------------------------------------------------------------------------*/
void
shell_start(void)
{
  /* set BBS parameters */
  /*bbs_status.bbs_board_id=1;
  bbs_status.bbs_msg_id=1;
  process_start(&bbs_timer_process, NULL);*/
  bbs_lock();

  if(bbs_locked == 1) {
    shell_exit(); 
    log_message("[bbs] *busy*","");
  } else {
    bbs_locked=1;
    bbs_banner(BBS_BANNER_LOGIN);
    shell_output_str(NULL, "\n\rContiki BBS " , BBS_STRING_VERSION);

    shell_prompt("\n\rlogin: ");
    front_process=&bbs_login_process;
  } 


}
/*---------------------------------------------------------------------------*/
void
shell_stop(void)
{
   if (bbs_locked==1) {
      log_message("[bbs] ", "*timeout*");
   }

   /* set BBS parameters */
   bbs_locked=0;
   bbs_status.bbs_status=0;
   bbs_status.bbs_board_id=1;
   bbs_status.bbs_msg_id=1;
   killall();
}
/*---------------------------------------------------------------------------*/
void
shell_quit(void)
{
  process_exit(&bbs_login_process);
  process_exit(&bbs_timer_process);
  process_exit(&shell_process);
  process_exit(&shell_server_process);
  shell_stop();
}
/*---------------------------------------------------------------------------*/

/** @} */
