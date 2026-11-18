/*** includes ***/
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <asm-generic/ioctls.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

/*** defines ***/
#define CENTI_VERSION "0.0.1"
#define CTRL_KEY(k) ((k) & 0x1f) // bitmask to clear 5th and 6th bit to 0, imitating a CTRL key

enum editorKey{
    ARROW_LEFT = 1000, //value that no key can reach
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    DEL_KEY,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN
};

/*** data ***/
// datatype for storing a row of text in our editor
typedef struct erow{
    int size;
    char *chars;
} erow; //editor row

// storing original terminal flags to reset it later
struct editorConfig{
    int cx, cy; // mouse position
    int screenrows;
    int screencols;
    int numrows;
    erow *row;
    struct termios orig_termios;
};
struct editorConfig E;

/*** terminal ***/

// a failed function will set global errno value to something, perror prints an error for it according to this
// it also prints the string given to it before it dies to give you context
void die(const char *s){
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3); //reset everything before crashing
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
int editorReadKey(){
    int nread;
    char c;
    while((nread = read(STDIN_FILENO, &c, 1)) != 1){
        if(nread == -1 && errno != EAGAIN) die("read");
    }
    if (c == '\x1b'){ //escape sequence
        char seq[3]; //allocate 3 bytes

        if(read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if(read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

        if(seq[0] == '['){
            if(seq[1] >= '0' && seq[1] <= '9'){
                if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
                if(seq[2] == '~'){
                    switch(seq[1]){
                        case '1': return HOME_KEY;
                        case '3': return DEL_KEY;
                        case '4': return END_KEY;
                        case '5': return PAGE_UP;
                        case '6': return PAGE_DOWN;
                        case '7': return HOME_KEY;
                        case '8': return END_KEY;
                    }
                }
            }
            else{
                switch(seq[1]){
                    case 'A': return ARROW_UP;
                    case 'B': return ARROW_DOWN;
                    case 'C': return ARROW_RIGHT;
                    case 'D': return ARROW_LEFT;
                    case 'H': return HOME_KEY;
                    case 'F': return END_KEY;
                }
            }
        }
        else if(seq[0] == 'O'){
            switch(seq[1]){
                case 'H': return HOME_KEY;
                case 'F': return END_KEY;
            }
        }
        return '\x1b';
    }
    else{
        return c;
    }
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
int getWindowSize(int *rows, int *cols){ //change pointer value to have more flexible return values
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

void editorAppendRow(char *s, size_t len){
    E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));

    int at = E.numrows;
    E.row[at].size = len;
    E.row[at].chars = malloc(len + 1);
    memcpy(E.row[at].chars, s, len);
    E.row[at].chars[len] = '\0';
    E.numrows++;
}

/*** file i/o ***/
void editorOpen(char *filename){
    FILE *fp = fopen(filename, "r");
    if(!fp) die("fopen");

    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;
    while((linelen = getline(&line, &linecap, fp)) != -1){
        while(linelen > 0 && (line[linelen -1] == '\n' || line[linelen - 1] == '\r'))
            linelen--;
        editorAppendRow(line, linelen);
    }
    free(line);
    fclose(fp);
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
    char *new = realloc(ab->b, ab->len + len); //allocates more space
    if(new == NULL) return;
    memcpy(&new[ab->len], s, len); //copies string to the end
    ab->b = new;
    ab->len += len; //updates data. this string doesnt have null terminator so a length is needed
}

void abFree(struct abuf *ab){
    free(ab->b);
}
/*** output ***/

void editorDrawRows(struct abuf *ab){
    int y;
    for(y = 0; y < E.screenrows; y++){ //goes upto 1/3rd of screen
        if(y >= E.numrows){
            if(E.numrows == 0 && y == E.screenrows / 3){
                char welcome[80];
                int welcomelen = snprintf(welcome,sizeof(welcome),"Centi editor -- version %s", CENTI_VERSION);
                if(welcomelen > E.screencols) welcomelen = E.screencols;
                int padding = (E.screencols - welcomelen) / 2; //puts it in middle
                if (padding) {
                    abAppend(ab, "~", 1);
                    padding--; //offsets padding by 1 to add a ~
                }
                while (padding--) abAppend(ab, " ", 1);
                abAppend(ab, welcome, welcomelen);
            }
            else {
                abAppend(ab, "~", 1);
            }
        }
        else{
            int len = E.row[y].size;
            if(len > E.screencols) len = E.screencols;
            abAppend(ab, E.row[y].chars, len);
        }
        abAppend(ab, "\x1b[K", 3); //K: erase in line, erases part of the current line, here we erase to right of cursor
        if(y < E.screenrows - 1){
            abAppend(ab, "\r\n", 2);
        }
    }
}
void editorRefreshScreen(){
    // write 4 bytes, \x1b is an escape sequence, 27 in decimal, and then [2J
    // escape seq always starts with 27[. we are using VT100 escape sequences
    // h: set, l: reset. used to toggle various modes. ?25 means cursor hiding
    // J command is Erase in Display which clears the screen. it takes argument 2, which means the entire screen.
    // H command is cursor position, default argument 1;1 which puts cursor at 1,1
    struct abuf ab = ABUF_INIT;
    abAppend(&ab, "\x1b[?25l", 6); //hide cursor
    //abAppend(&ab, "\x1b[2J", 4); // only need to clear updated lines
    abAppend(&ab, "\x1b[H", 3);

    editorDrawRows(&ab);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy+1, E.cx+1); //puts cursor over x,y
    abAppend(&ab, buf, strlen(buf));

    abAppend(&ab, "\x1b[?25h", 6); //show cursor
    write(STDOUT_FILENO,ab.b , ab.len);
    abFree(&ab);
}

/*** input ***/

void editorMoveCursor(int key){
    switch (key){
        case ARROW_LEFT:
            if(E.cx != 0){
                E.cx--;}
            break;
        case ARROW_RIGHT:
            if(E.cx != E.screencols -1){
                E.cx++;}
            break;
        case ARROW_UP:
            if(E.cy != 0){
                E.cy--;}
            break;
        case ARROW_DOWN:
            if(E.cy != E.screenrows -1){
                E.cy++;}
            break;
    }
}

// exit when ctrl + q
void editorProcessKeypress(){
    int c = editorReadKey();

    switch(c){
        case CTRL_KEY('q'):
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;
        case HOME_KEY:
            E.cx = 0;
            break;
        case END_KEY:
            E.cx = E.screencols - 1;
            break;
        case PAGE_UP:
        case PAGE_DOWN:
            {
                int times = E.screenrows;
                while(times--)
                    editorMoveCursor(c == PAGE_UP? ARROW_UP: ARROW_DOWN);
            }
            break;
        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
            editorMoveCursor(c);
            break;
    }
}


/*** init ***/

void initEditor(){
    E.cx = 0;
    E.cy = 0;
    E.numrows = 0;
    E.row = NULL;

    if (getWindowSize(&E.screenrows, &E.screencols) == -1) die ("getWindowSize");
}

int main(int argc, char *argv[]){
    enableRawMode();
    initEditor();
    if(argc >= 2){
        editorOpen(argv[1]);
    }
    while(1){
        editorRefreshScreen();
        editorProcessKeypress();
    }
    return 0;
}
