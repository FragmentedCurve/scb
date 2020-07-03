#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <ncurses.h>

#define ENTER_KEY 0x0A
#define DEFAULT_EDITOR "vim"
#define SCROLL_UP BUTTON4_PRESSED
#define SCROLL_DOWN 0x200000

struct Lines {
	char** lines;
	size_t len;
	size_t cap;
	size_t selected;
};

struct Lines* new_lines()
{
	const size_t cap = 1024;
	struct Lines* l = calloc(1, sizeof (struct Lines));
	assert(l);

	l->lines = malloc(cap * sizeof (char*));
	l->cap = cap;
	l->selected = -1;

	return l;
}

void add_line(struct Lines* l, char* line)
{
	size_t len = strlen(line);
	
	if (l->len == l->cap) {
		l->cap *= 2;
		l->lines = realloc(l->lines, l->cap);
		assert(l->lines);
	}

	if (line[len - 1] == '\n') {
		line[len - 1] = 0;
	}
	l->lines[l->len] = line;
	l->len++;
}

void select_down(struct Lines* l, size_t n)
{
	if ((l->selected + n) < l->len)
		l->selected += n;
	else
		l->selected = l->len - 1;
}

void select_up(struct Lines* l, size_t n)
{
	if (l->selected >= n)
		l->selected -= n;
	else
		l->selected = 0;
}

void reset_stds()
{
	FILE* fd = fopen("/dev/tty", "r+");
	close(0);
	dup(fileno(fd));
	close(1);
	dup(fileno(fd));
	fclose(fd);
}

void init_screen()
{
	reset_stds();
	initscr();
	cbreak();
	noecho();
	curs_set(0);
	clear();
}

void auto_select(struct Lines* l)
{
	int rows, col;
	
	getmaxyx(stdscr, rows, col);

	if (l->len < rows) {
		l->selected = l->len / 2;
	} else {
		l->selected = rows / 2;
	}
}

void draw_line(int row, const char* line)
{
	attrset(A_NORMAL);
	mvprintw(row, 0, "%s", line);
}

void draw_selected(int row, const char* line)
{
	int rows, cols;
	int r,c ;
	size_t len = strlen(line);

	attrset(A_STANDOUT);
	getmaxyx(stdscr, rows, cols);
	mvprintw(row, 0, "%s", line);
	getyx(stdscr, r, c);
	for (int i = c; i < cols; i++) {
		addch(' ');
	}
}

void draw_frame(struct Lines* l, size_t offset)
{
	int rows, cols;
	getmaxyx(stdscr, rows, cols);
	
	for (int i = offset; (i - offset) < rows && i < l->len; i++) {
		if (i == l->selected) {
			draw_selected(i - offset, l->lines[i]);
		} else {
			draw_line(i - offset, l->lines[i]);
		}
	}
}

void draw(struct Lines* l)
{
	size_t offset;
	int rows, cols;
	
	clear();
	getmaxyx(stdscr, rows, cols);

	if (l->selected <= rows / 2) {
		offset = 0;
	} else if (l->selected >= l->len - (rows / 2)){
		offset = l->len - rows;		
	} else {
		offset = l->selected - (rows / 2);
	}

	draw_frame(l, offset);
	refresh();
}

void parse_line(char* line, char** file, char** linenum)
{
	char* tok = strchr(line, ':');
	
	*file = NULL;
	*linenum = NULL;
	
	if (!tok) {
		*file = strdup(line);
		return;
	}

	*file = strndup(line, tok - line);
	tok = strchr(tok + 1, ':');
	if (!tok) {
		*linenum = NULL;
		return;
	}
	*linenum = line + strlen(*file) + 1;
	*linenum = strndup(*linenum - 1, tok - *linenum + 1);
	**linenum = '+';
}

void open_line(char* line)
{
	char* file;
	char* linenum;
	char* editor = getenv("EDITOR");
	
	if (!editor) {
		editor = DEFAULT_EDITOR;
	}
	parse_line(line, &file, &linenum);

	if (!file || !linenum) {
		clear();
		mvprintw(0, 0, "Error parsing line.");
		refresh();
		getch();
		return;
	}
	
	/* TODO: Do error checking and reporting on the parsed line. */
	
	def_prog_mode();
	endwin();
	pid_t pid = fork();
	if (pid > 0) {
		free(file);
		free(linenum);
		waitpid(pid, NULL, 0);
		reset_prog_mode();
		refresh();
	} else if (pid == 0){		
		if (execlp(editor, editor, linenum, file, NULL) == -1) {
			fprintf(stderr, "Failed to run %s: %s\n", editor, strerror(errno));
			exit(1);
		}
	} else {
		fprintf(stderr, "Failed to fork: %s\n", strerror(errno));
	}
}

void input_loop(struct Lines* l)
{
	int c;

	mousemask(ALL_MOUSE_EVENTS, NULL);
	keypad(stdscr, TRUE);
	auto_select(l);
	draw(l);

	while ((c = getch()) != 'q') {
		switch (c) {
			case KEY_MOUSE: {
				MEVENT event;
				if (getmouse(&event) == OK) {
					if (event.bstate & BUTTON3_CLICKED) {
						open_line(l->lines[l->selected]);
					} else if (event.bstate & SCROLL_UP) {
						select_up(l, 1);
					} else if (event.bstate & SCROLL_DOWN) {
						select_down(l, 1);
					}
				}
			} break;
			case KEY_UP: {
				select_up(l, 1);
			} break;
			case KEY_DOWN: {
				select_down(l, 1);
			} break;
			case KEY_PPAGE: {
				int rows, col;
				getmaxyx(stdscr, rows, col);
				select_up(l, rows / 2);
			} break;
			case KEY_NPAGE: {
				int rows, col;
				getmaxyx(stdscr, rows, col);
				select_down(l, rows / 2);
			} break;
			case ENTER_KEY: {
				open_line(l->lines[l->selected]);
			} break;
		}
		draw(l);
	}
}

int main(int argc, char** argv)
{
	struct Lines* l = new_lines();

	{
		char* line = NULL;
		size_t len = 0;
		
		while (getline(&line, &len, stdin) != -1) {
			add_line(l, line);
			line = NULL;
			len = 0;
		}
	}

	if (l->len == 0) {
		fprintf(stderr, "Nothing to do.\n");
		return 0;
	}
	
	init_screen();
	input_loop(l);
	endwin();
	return 0;
}