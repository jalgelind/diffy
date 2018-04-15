#include "tty.hpp"

#include <string>

#ifdef DIFFY_PLATFORM_POSIX
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

using namespace diffy;

// TODO: Something smarter; see e.g:
// https://gist.github.com/jtriley/1108174
// TODO: Error handling
void
diffy::get_term_size(int* rows, int* cols) {
#ifdef DIFFY_PLATFORM_POSIX
    // TODO: Clear errno where appropriate.
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0) {
        *rows = w.ws_row;
        *cols = w.ws_col;
        return;
    }

    auto term_fd = open(ctermid(NULL), O_RDONLY);
    if (term_fd > 0 && ioctl(term_fd, TIOCGWINSZ, &w) == 0) {
        *rows = w.ws_row;
        *cols = w.ws_col;
        close(term_fd);
        return;
    }
    if (term_fd)
        close(term_fd);

    char* env_cols = getenv("COLUMNS");
    char* env_rows = getenv("LINES");
    if (env_cols && env_rows) {
        *cols = std::atoi(env_cols);
        *rows = std::atoi(env_rows);
        return;
    }
#else
    // TODO: There's probably some win32 api for this.
    *cols = 80;
    *rows = 50;
#endif
}

TerminalColorCapability
diffy::get_terminal_color_capability() {
#ifdef DIFFY_PLATFORM_WINDOWS
    // TODO: ?
#else
    // TODO: Use libterminfo?

    // For 24 bit colors, check env var 'COLORTERM' for 'truecolor' or '24bit'

    // If we're not outputting to a terminal, we don't output any colors.
    // NOTE: This will prevent colored output when piping to less or when
    //       redirecting to files.
    if (isatty(STDOUT_FILENO) == 0) {
        // TODO: clear errno
        return TerminalColorCapability::None;
    }

    // The COLORTERM variable is usually available to indicate 24bit color support.
    const char* colorterm_var = getenv("COLORTERM");
    if (colorterm_var != nullptr) {
        const std::string& colorterm(colorterm_var);
        if (colorterm == "24bit" || colorterm == "truecolor") {
            return TerminalColorCapability::Ansi24bit;
        }
    }

    // And if that's not supported, fall back to checking terminfo with tput.
    FILE* pipe = popen("tput colors 2>&1", "r");  // stderr isn't captured, so redirect it to stdout.
    if (pipe) {
        char buffer[16];
        bool buffer_valid = fgets(buffer, 16, pipe) != nullptr;
        pclose(pipe);
        if (buffer_valid) {
            // NOTE: atoi returns 0 on failure
            int colors = std::atoi(buffer);
            switch (colors) {
                case 16:
                    return TerminalColorCapability::Ansi4bit;
                case 256:
                    return TerminalColorCapability::Ansi8bit;
                default:
                    break;
            }
        }
    }
#endif
    return TerminalColorCapability::None;
}