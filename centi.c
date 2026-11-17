/*** includes ***/
#include <asm-generic/ioctls.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

/*** defines ***/
#define CTRL_KEY(k) ((k) & 0x1f) // bitmask to clear 5th and 6th bit to 0, imitating a CTRL key

/*** data ***/

// storing original terminal flags to reset it later
struct editorConfig{
    int screenrows;
    int screencols;
    struct termios orig_termios;
};
struct editorConfig E;

/*** terminal ***/

// a failed function will set global errno value to something, perror prints an error for it according to this
// it also prints the string given to it before it dies to give you context
void die(const char *s){
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    perror(s);
    exit(1);
}
// to flip all flags back
void disableRawMode(){
    // STDIN_FILENO is a macro that evaluates to 0 representing file inp, which in a terminal is ur keyboard
    // TCSAFLUSH flushes out stray input before something happens
    // returns -1 if errors
    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1) die("tcsetattr");
}
//to not echo keypresses
void enableRawMode(){
    if(tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tcgetattr"); // get attributes of fresh term
    atexit(disableRawMode); // at exit, reset term state

    struct termios raw = E.orig_termios; // temporary make a copy of orig attributes
    raw.c_lflag &= ~(BRKINT | INPCK | ISTRIP | ICRNL | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= ~(CS8);
    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
    // echo is 0100, ~= 1011, and= disables 3rd bit. common convention to flip bits
    // ECHO: flag to repeat the inputs
    // ICANON: turn off canonical mode and makes it raw mode, instead of storing input in a buffer (allowing user to edit), we straight up feed it thus the program can understand it byte by byte
    // ISIG: turns off Ctrl + C and Z (C = kill, Z = send to bg) and parses them as just bytes
    // IXON: turns iff Ctrl + S and Q (S = pause, Q = resume inp) I stands for input flags, unlike the other
    // IEXTEN: turns off Ctrl + V (V = sends the next thing literally)
    // ICRNL: turn off inp CR-NL, turns off terminals helpful behaviour of interpreting \r as \n. for this reason, Ctrl + M is affected. it originally converts carriage return to a newline. carriage return is basically moving cursor to the start of the same line, overwriting anything on it.
    // OPOST: output post processing, stops translating \n to \r\n. should change it in all printf()s
    // BRKINT, INPCK, ISTRIP, and CS8: they are like old traditions so we keep them anyway
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;
    // c_cc stands for control character, an array of bites that control various terminal settings
    // VMIN sets the minimum no of bytes before read() can return, read() returns as soon as there is an input to be read
    // VTIME sets the max amount of time to wait before read() returns, its 100ms, so if read() times out, it returns 0
    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr"); // set attr with ECHO flipped off
}
// in cygwin, errno has to be explicitly EAGAIN because it nread gives -1 when timedout, so this avoids false positive
char editorReadKey(){
    int nread;
    char c;
    while((nread = read(STDIN_FILENO, &c, 1)) != 1){
        if(nread == -1 && errno != EAGAIN) die("read");
    }
    return c;
}
int getCursorPosition(int *rows, int *cols){
    char buf[32];
    unsigned int i = 0;

    if(write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1; // n is device status report, 6 means cursor pos
    
    while(i < sizeof(buf) -1){
        if(read(STDIN_FILENO, &buf[i], 1) != 1) break;
        if(buf[i] == 'R') break;
        i++;
    }
    buf[i] = '\0';

    if(buf[0] != '\x1b' || buf[1] != '[') return -1;
    if(sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;

    return 0;
}
int getWindowSize(int *rows, int *cols){
    struct winsize ws;
    //from output, we get terminal inp/out control get window size. change the parameter when success or ret -1
    if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0){
        if(write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1; //send mouse to bottom right
        return getCursorPosition(rows, cols);
    }
    else{
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}
/*** append buffer ***/
//we need a dynamic string type to write everything at once instead of line by line
struct abuf {
    char *b;
    int len;
};
//init
#define ABUF_INIT {NULL,0}
//takes our custom datatype, reallocs a bigger spaces an then copies new string at the end of old string
void abAppend(struct abuf *ab, const char *s, int len){
    char *new = realloc(ab->b, ab->len + len);
    if(new == NULL) return;
    memcpy(&new[ab->len], s, len);
    ab->b = new;
    ab->len += len;
}

void abFree(struct abuf *ab){
    free(ab->b);
}
/*** output ***/

void editorDrawRows(struct abuf *ab){
    int y;
    for (y = 0; y < E.screenrows; y++){
        abAppend(ab, "~", 1);

        if(y < E.screenrows - 1){
            abAppend(ab, "\r\n", 2);
        }
    }
}
void editorRefreshScreen(){
    // write 4 bytes, \x1b is an escape sequence, 27 in decimal, and then [2J
    // escape seq always starts with 27[. we are using VT100 escape sequences
    // J command is Erase in Display which clears the screen. it takes argument 2, which means the entire screen.
    // H command is cursor position, default argument 1;1 which puts cursor at 1,1
    struct abuf ab = ABUF_INIT;
    abAppend(&ab, "\x1b[2J", 4);
    abAppend(&ab, "\x1b[H", 3);

    editorDrawRows(&ab);
    abAppend(&ab, "\x1b[H", 3);
    write(STDOUT_FILENO,ab.b , ab.len);
    abFree(&ab);
}

/*** input ***/

// exit when ctrl + q
void editorProcessKeypress(){
    char c = editorReadKey();

    switch(c){
        case CTRL_KEY('q'):
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;
    }
}


/*** init ***/

void initEditor(){
    if (getWindowSize(&E.screenrows, &E.screencols) == -1) die ("getWindowSize");
}

int main(){
    enableRawMode();
    initEditor();

    while(1){
        editorRefreshScreen();
        editorProcessKeypress();
    }
    return 0;
}
