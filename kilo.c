#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>

#define CTRL_KEY(k) ((k) & 0x1f)

// original state of the terminal attributes
struct termios orig_termios;

void die(const char *s) {
    perror(s);
    exit(1);
}

// Disable Raw Mode
void disableRawMode(){
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
    die("tcsetattr");
}

// Enable Raw Mode
void enableRawMode() {

    // Make a copy of the original terminal state
    if(tcgetattr(STDIN_FILENO, &orig_termios) == -1) die("tcgetattr");

    // This is used to register the disableRawMode method
    // so that it will be executed when the program exits.
    atexit(disableRawMode);

    struct termios raw = orig_termios;
    
    // Disable software flow control signals like Ctrl-S and Ctrl-Q
    raw.c_iflag &= ~(BRKINT | INPCK | ISTRIP | IXON | ICRNL);

    raw.c_oflag &= ~(OPOST);

    raw.c_cflag |= (CS8);

    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    // Flip the ECHO bitflag to disable ECHO, Control signals
    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}

int main() {
    enableRawMode();
    
    while(1) {
        char c = '\0';
        if(read(STDIN_FILENO, &c,1) == -1 && errno != EAGAIN) die("read");
        if(iscntrl(c)) {
            printf("%d\r\n", c);
        } else {
            printf("%d ('%c')\r\n", c, c);
        }
        if (c == CTRL_KEY('q')) break;
    }

    return 0;
}
