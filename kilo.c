/**
 * Paused at: Chapter 4: Step 69
 */ 

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

/*** includes ***/
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>
#include <termios.h>

/*** defines ***/
#define CTRL_KEY(k) ((k) & 0x1f)
#define KILO_VERSION "0.0.1"

enum editorKey {
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    HOME_KEY,
    END_KEY,
    DEL_KEY,
    PAGE_UP,
    PAGE_DOWN
};

/*** data ***/
typedef struct erow {
    int size;
    char *chars;
} erow;

struct editorConfig {
    int cx, cy;
    int rowoff; // row offset
    int screenrows;
    int screencols;
    int numrows;
    erow *row;
    // original state of the terminal attributes
    struct termios orig_termios;
};

struct editorConfig E;

/*** terminal ***/
void die(const char *s) {
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    perror(s);
    exit(1);
}

// Disable Raw Mode
void disableRawMode(){
    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1) die("tcsetattr");
}

// Enable Raw Mode
void enableRawMode() {

    // Make a copy of the original terminal state
    if(tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tcgetattr");

    // This is used to register the disableRawMode method
    // so that it will be executed when the program exits.
    atexit(disableRawMode);

    struct termios raw = E.orig_termios;
    
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

/**
 * Returns the key pressed
 */
int editorReadKey() {
    int nread;
    char c;
    while((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if(nread == -1 && errno != EAGAIN) die("read");
    }

    if(c == '\x1b') {
        char seq[3];

        if(read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if(read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

        if(seq[0] == '[') {
            if(seq[1] >= '0' && seq[1] <= '9') {
                if(read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
                if(seq[2] == '~') {
                    switch(seq[1]) {
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
            else {
                switch(seq[1]) {
                    case 'A': return ARROW_UP;
                    case 'B': return ARROW_DOWN;
                    case 'C': return ARROW_RIGHT;
                    case 'D': return ARROW_LEFT;
                }    
            }
        } else if(seq[0] == 'O') {
            switch(seq[1]) {
                case 'H': return HOME_KEY;
                case 'F': return END_KEY;
            }
        }

        return '\x1b';
    } else {
        return c;    
    }
}

// Get the cursor position on the terminal
int getCursorPosition(int *rows, int *cols) {
    
    char buf[32];
    unsigned int i = 0;
    
    if(write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;

    while (i < sizeof(buf) - 1) {
        if(read(STDIN_FILENO, &buf[i], 1) != 1) break;
        if(buf[i] == 'R') break;
        i++;
    }

    // Any string should end with 0 bytes, so the final byte will be null.
    buf[i] = '\0';

    // printf("\r\n&buf[1]: `%s'\r\n", &buf[1]);

    if(buf[0] != '\x1b' || buf[1] != '[') return -1;
    if(sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;

    // printf("\r\n");
    // char c;
    // while (read(STDIN_FILENO, &c, 1) == 1) {
    //     if(iscntrl(c)) {
    //         printf("%d\r\n", c);
    //     } else {
    //         printf("%d ('%c')\r\n", c, c);
    //     }
    // }

    // editorReadKey();
    return 0;
}

// Get the window size of the terminal
int getWindowSize(int *rows, int *cols) {
    struct winsize ws;

    if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        if(write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
        return getCursorPosition(rows, cols);
    } else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

void editorAppendRow(char *s, size_t len) {
    /**
     * Allocate memory for each row with the following equation:
     * (num. of bytes for each row * the number of rows needed)
     */
    E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));

    int at = E.numrows;
    E.row[at].size = len;
    E.row[at].chars = malloc(len + 1);
    memcpy(E.row[at].chars, s, len);
    E.row[at].chars[len] = '\0';
    E.numrows++;
}

void editorOpen(char *filename) {
    FILE *fp = fopen(filename, "r");
    if(!fp) die("fopen");

    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;
    

    while((linelen = getline(&line, &linecap, fp)) != -1) {
        while(linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r')) {
            linelen--;
        }
        editorAppendRow(line, linelen);
    }

    free(line);
    fclose(fp);
}

/*** append buffer ***/
struct abuf {
    char *b;
    int len;
};

#define ABUF_INIT {NULL, 0}

void abAppend(struct abuf *ab, const char *s, int len) {
    // Allocate enough memory to hold the new string
    char *new = realloc(ab->b, ab->len + len);

    if (new == NULL) return;
    memcpy(&new[ab->len], s, len);
    ab->b = new;
    ab->len += len;
}

void abFree(struct abuf *ab) {
    free(ab->b);
}

void editorScroll() {
    if(E.cy < E.rowoff) {
        E.rowoff = E.cy;
    }

    if(E.cy >= E.rowoff + E.screenrows) {
        E.rowoff = E.cy - E.screenrows + 1;
    }
}

/*** output ***/
void editorDrawRows(struct abuf *ab) {
    int y;
    for (y = 0; y<E.screenrows; y++) {
        int filerow = y + E.rowoff;
        if(filerow >= E.numrows) {
            if(E.numrows == 0 && y == E.screenrows / 3) {
                char welcome[80];
                int welcomelen = snprintf(welcome, sizeof(welcome), "Kilo Editor -- MCB Edition %s", KILO_VERSION);
                if(welcomelen > E.screencols) welcomelen = E.screencols;
                
                // Add some padding to center the title
                int padding = (E.screencols - welcomelen) / 2;
                if(padding) {
                    abAppend(ab, "~", 1);
                    padding--;
                }
                while(padding--) abAppend(ab, " ", 1);
                
                abAppend(ab, welcome, welcomelen);
            } else {
                abAppend(ab, "~", 1);
            }
        } else {
            int len = E.row[filerow].size;
            if(len > E.screencols) len = E.screencols;
            abAppend(ab, E.row[filerow].chars, len);
        }
        
        abAppend(ab, "\x1b[K", 3);
        if(y < E.screenrows - 1) {
            abAppend(ab, "\r\n", 2);
        }
    }
}

// Clear the entire display -- This makes use of VT100 escape sequences.
void editorRefreshScreen() {

    editorScroll();

    struct abuf ab = ABUF_INIT;
    abAppend(&ab, "\x1b[?25l", 6);
    // abAppend(&ab, "\x1b[2J", 4);
    abAppend(&ab, "\x1b[H", 3);

    editorDrawRows(&ab);
    
    // Cursor movement
    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, E.cx + 1);
    abAppend(&ab, buf, strlen(buf));

    // abAppend(&ab, "\x1b[H", 3);
    abAppend(&ab, "\x1b[?25h", 6);

    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
}



/*** input ***/

/**
 * Using WASD keys to move in the editor.
 */ 
void editorMoveCursor(int key) {
    switch(key) {
        case ARROW_UP:
            if(E.cy != 0) E.cy--;
            break;
        
        case ARROW_LEFT:
            if(E.cx != 0) E.cx--;
            break;
        
        case ARROW_DOWN:
            if(E.cy < E.numrows) E.cy++;
            break;

        case ARROW_RIGHT:
            if(E.cx != E.screencols - 1) E.cx++;
            break;
    }
}

// Event handler to check for key press event
void editorProcessKeyPress() {
    int c = editorReadKey();
    switch(c) {
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
            while(times--) {
                editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
            }
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

void initEditor() {
    E.cx = 0;
    E.cy = 0;
    E.rowoff = 0;
    E.numrows = 0;
    E.row = NULL;
    if(getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
}

int main(int argc, char *argv[]) {
    enableRawMode();
    initEditor();

    if(argc >= 2) {
        editorOpen(argv[1]);    
    }
    
    while(1) {
        editorRefreshScreen();
        editorProcessKeyPress();
    }

    return 0;
}