/*** includes ***/
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

/*** data ***/

//storing original terminal flags to reset it later
struct termios orig_termios;

/*** terminal ***/

// a failed function will set global errno value to something, perror prints an error for it according to this
// it also prints the string given to it before it dies to give you context
void die(const char *s){
    perror(s);
    exit(1);
}
//to flip all flags back
void disableRawMode(){
    //STDIN_FILENO is a macro that evaluates to 0 representing file inp, which in a terminal is ur keyboard
    //TCSAFLUSH flushes out stray input before something happens
    //returns -1 if errors
    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1) die("tcsetattr");
}
//to not echo keypresses
void enableRawMode(){
    if(tcgetattr(STDIN_FILENO, &orig_termios) == -1) die("tcgetattr"); // get attributes of fresh term
    atexit(disableRawMode); // at exit, reset term state

    struct termios raw = orig_termios; // temporary make a copy of orig attributes
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
    //c_cc stands for control character, an array of bites that control various terminal settings
    //VMIN sets the minimum no of bytes before read() can return, read() returns as soon as there is an input to be read
    //VTIME sets the max amount of time to wait before read() returns, its 100ms, so if read() times out, it returns 0
    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr"); // set attr with ECHO flipped off
}

/*** init ***/

int main(){
    enableRawMode();
    while(1){
        char c = '\0';
        if(read(STDIN_FILENO, &c, 1) == -1 && errno != EAGAIN) die("read"); // to fix a cygwin edgecase, made explicit
        if(iscntrl(c)){
            printf("%d\r\n",c);
        }
        else{
            printf("%d ('%c')\r\n",c,c);
        }
        if(c == 'q') break;
    }
    return 0;
}
