/******************************************
 *                Includes                *
 ******************************************/
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <pwd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <termios.h>
#include <linux/limits.h>
#include <dirent.h>
#include "vector.h"

/******************************************
 *                Defines                 *
 ******************************************/
#define CMD_SIZE 1024
#define PROMPT_MAX _SC_LOGIN_NAME_MAX + PATH_MAX
#define CLEAR_LINE      "\033[2K"
#define CLEAR_SCREEN    "\033[2J\033[H"
#define CLEAR_COLOR     "\033[39m"
#define COLOR_RED       "\033[38;5;124m"
#define COLOR_GREEN     "\033[38;5;40m"
#define COLOR_BLUE      "\033[38;5;27m"

/******************************************
 *      Helper Function Declarations      *
 ******************************************/
bool getInput(char* buffer, size_t size, Vector history, int pos);
void tabComplete(char* buffer, size_t size, int* i);
size_t printPrompt(void);
Vector tokenizeInput(char* input, size_t size);
void processTokens(Vector* tokens, int numCmds);
bool checkBuiltinCmd(Vector* tokens, int numCmds);
void homeDirSubstitution(char** pInput, size_t size);
bool checkRedirection(Vector tokens);
int countPipes(Vector tokens);
void extractPath(char* input, int inputSize, char** path);
Vector findAutofillStrings(const char* input, size_t size, const char* path);
void findLongestCommonPrefix(Vector autofills, char* buffer, size_t size);

struct termios old;

/******************************************
 *              Main Function             *
 ******************************************/
int main(void)
{
    struct termios term;
    tcgetattr(STDIN_FILENO, &old);
    term = old;

    term.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &term);

    Vector history = vectorInit(128);

    int fdIn = dup(STDIN_FILENO);
    int fdOut = dup(STDOUT_FILENO);

    while(1) {
        size_t len = printPrompt();
        if(len == 0)
            break;

        char input[CMD_SIZE] = {0};
        bool done = getInput(input, CMD_SIZE, history, len);
        if(!done)
            break;

        if(strnlen(input, CMD_SIZE) != 0) {
            if(history.size == 0 || strncmp(input, history.arr[history.size - 1], strnlen(input, CMD_SIZE)) != 0)
                vectorInsert(&history, input, strnlen(input, CMD_SIZE));

            Vector tokens = tokenizeInput(input, CMD_SIZE);

            for(size_t i = 0; i < tokens.size; i++)
                homeDirSubstitution(&tokens.arr[i], strnlen(tokens.arr[i], CMD_SIZE));

            int numCmds = countPipes(tokens) + 1;
            bool redir = checkRedirection(tokens);

            Vector commands[numCmds];
            if(numCmds > 1) {
                for(int i = 0; i < numCmds; i++) {
                    commands[i] = vectorInit(0);
                    if(commands[i].capacity == 0) {
                        for(int j = 0; j < i; j++)
                            vectorDestroy(&commands[j]);

                        vectorDestroy(&tokens);
                        continue;
                    }
                }

                int idx = 0;
                for(size_t i = 0; i < tokens.size; i++) {
                    if(strncmp(tokens.arr[i], "|", 2) == 0) {
                        idx++;
                    } else {

                        vectorInsert(&commands[idx], tokens.arr[i], strnlen(tokens.arr[i], CMD_SIZE));
                    }
                }
            } else if(numCmds == 1) {
                commands[0] = vectorInit(0);
                for(size_t i = 0; i < tokens.size; i++)
                    vectorInsert(&commands[0], tokens.arr[i], strnlen(tokens.arr[i], CMD_SIZE));
            }

            if(!checkBuiltinCmd(commands, numCmds))
                processTokens(commands, numCmds);

            if(redir || numCmds > 1) {
                dup2(fdIn, STDIN_FILENO);
                dup2(fdOut, STDOUT_FILENO);
            }

            for(int i = 0; i < numCmds; i++)
                vectorDestroy(&commands[i]);

            vectorDestroy(&tokens);
        }
    }
    printf("\n");

    vectorDestroy(&history);
    tcsetattr(STDIN_FILENO, TCSANOW, &old);

    return 0;
}

/******************************************
 *      Helper Function Definitions       *
 ******************************************/

/************************************************
 * getInput: Parse keyboard input one character
 * at a time. Special characters are handled in
 * this function
 *
 * buffer:  Input buffer which holds the
 *          characters to be displayed
 *
 * size:    Size of buffer
 *
 * history: A vector of all previously entered
 *          commands
 *
 * pos:     Current column in the terminal
 *
 * return:  Whether the program should continue
 *          or not. True continues and processes
 *          the current buffer, false ends the
 *          program
 ***********************************************/
bool getInput(char* buffer, size_t size, Vector history, int pos)
{
    if(!buffer || size == 0)
        return false;

    char c;
    int i = 0;
    size_t historyPos = 0;
    while((c = getc(stdin)) != '\n') {
        if(c == 0x4) { // EOF
            return false;
        } else if(c == '\t') {
            tabComplete(buffer, size, &i);
        } else if(c == 0x1b) {
            c = getc(stdin);
            if(c == 0x5b) {
                c = getc(stdin);
                if(c == 'A') { // Up Arrow
                    if(historyPos + 1 > history.size)
                        continue;

                    printPrompt();
                    historyPos += 1;
                    snprintf(buffer, size, "%s", history.arr[history.size - historyPos]);
                    i = strnlen(buffer, size);
                } else if(c == 'B') { // Down Arrow
                    if(historyPos == 0) {
                        continue;
                    } else if(historyPos - 1 == 0) {
                        printPrompt();
                        historyPos -= 1;
                        memset(buffer, 0, size);
                        i = 0;
                    } else {
                        printPrompt();
                        historyPos -= 1;
                        snprintf(buffer, size, "%s", history.arr[history.size - historyPos]);
                        i = strnlen(buffer, size);
                    }
                }
            }
        } else if(c == 0xc) { // Ctrl-L
            printf(CLEAR_SCREEN);
            printPrompt();
        } else if(c == 0x7f || c == 0x8) { // Backspace
            if(i > 0) {
                buffer[--i] = ' ';
                printf("\r\033[%dC%s", pos, buffer);
                buffer[i] = '\0';
            }
        } else { // Normal character
            buffer[i++] = c;
        }

        printf("\r\033[%dC%s", pos, buffer);
    }

    printf("\n");
    return true;
}

/************************************************
 * tabComplete: Search the current directory, or
 *              a given path, for a match to a
 *              partially typed file/directory
 *
 * buffer:      Input buffer which holds
 *              characters to be displayed
 *
 * size:        Size of buffer
 *
 * i:           Current column in the terminal
 ***********************************************/
void tabComplete(char* buffer, size_t size, int* i)
{
    if(!i)
        return;

    char copy[size];
    strlcpy(copy, buffer, size);
    Vector toks = tokenizeInput(copy, *i);
    if(toks.size == 0)
        return;

    char* path = calloc(PATH_MAX, sizeof(char));
    if(!path) {
        vectorDestroy(&toks);
        return;
    }

    char* pathStr = toks.arr[toks.size - 1];
    extractPath(pathStr, strnlen(pathStr, CMD_SIZE), &path);

    char stub[PATH_MAX] = {0};
    int j = strnlen(pathStr, CMD_SIZE) - 1;
    for(; j >= 0; j--) {
        if(pathStr[j] == '/')
            break;
    }

    strlcpy(stub, pathStr + j + 1, PATH_MAX);
    Vector autofill = findAutofillStrings(stub, strnlen(stub, PATH_MAX), path);

    if(autofill.size == 1) {
        memset(buffer, 0, *i);

        for(size_t j = 0; j < toks.size - 1; j++) {
            strlcat(buffer, toks.arr[j], size);
            strlcat(buffer, " ", size);
        }

        if(strncmp(path, "./", 3) == 0) {
            if(strstr(toks.arr[toks.size-1], "./") == toks.arr[toks.size-1]) {
                strlcat(buffer, path, size); // Only add "./" to the buffer if the user typed it
            }
        } else {
            strlcat(buffer, path, size);
        }

        strlcat(buffer, autofill.arr[0], size);
        *i = strnlen(buffer, size);
    } else if(autofill.size > 1) {
        printf("\n");
        for(size_t j = 0; j < autofill.size; j++)
            printf("%s ", autofill.arr[j]);

        memset(buffer, 0, *i);
        for(size_t j = 0; j < toks.size - 1; j++) {
            strlcat(buffer, toks.arr[j], size);
            strlcat(buffer, " ", size);
        }

        if(strncmp(path, "./", 3) == 0) {
            if(strstr(toks.arr[toks.size-1], "./") == toks.arr[toks.size-1]) {
                strlcat(buffer, path, size); // Only add "./" to the buffer if the user typed it
            }
        } else {
            strlcat(buffer, path, size);
        }

        char lcp[FILENAME_MAX] = {0};
        findLongestCommonPrefix(autofill, lcp, FILENAME_MAX);
        strlcat(buffer, lcp, size);
        *i = strnlen(buffer, size);

        printf("\n");
        printPrompt();
    }

    free(path);
    vectorDestroy(&toks);
    vectorDestroy(&autofill);
}

/************************************************
 * printPrompt: Create a prompt from the username
 *              and path to current working
 *              directory
 *
 * return:      Length of the prompt
 ***********************************************/
size_t printPrompt(void)
{
    char user[_SC_LOGIN_NAME_MAX] = {0};
    int ret = getlogin_r(user, _SC_LOGIN_NAME_MAX);
    if(ret != 0) {
        perror("getlogin_r");
        return 0;
    }
    strcat(user, ":");

    char cwd[PATH_MAX] = {0};
    char* result = getcwd(cwd, PATH_MAX);
    if(!result) {
        perror("getcwd");
        return 0;
    }
    strcat(cwd, "$ ");

    char* homeDir = getenv("HOME");
    if(homeDir) {
        if(strstr(cwd, homeDir) == cwd) {
            char temp[PATH_MAX] = {0};
            strlcpy(temp, "~", PATH_MAX);
            strlcat(temp, cwd + strnlen(homeDir, PATH_MAX), PATH_MAX);
            strlcpy(cwd, temp, PATH_MAX);
        }
    }

    printf(CLEAR_LINE);
    printf("\033[G");
    printf(COLOR_BLUE "%s", user);
    printf(COLOR_GREEN "%s", cwd);
    printf(CLEAR_COLOR);

    return strnlen(user, _SC_LOGIN_NAME_MAX) + strnlen(cwd, PATH_MAX);
}

/************************************************
 * tokenizeInput:   Split the input buffer into
 *                  tokens on spaces
 *
 * input:           Buffer containing input
 *                  string
 *
 * size:            Size of input string
 *
 * return:          A vector containing input
 *                  tokens
 ***********************************************/
Vector tokenizeInput(char* input, size_t size)
{
    if(!input)
        return (Vector){0, 0, 0};

    Vector tokens = vectorInit(0);

    char* tok = strtok(input, " \n");
    while(tok) {
        vectorInsert(&tokens, tok, strnlen(tok, size));
        tok = strtok(NULL, " \n");
    }

    return tokens;
}

/************************************************
 * processTokens:   Process tokens which are not
 *                  built-in shell commands
 *
 * tokens:          Array of vectors holding the
 *                  tokenized user input
 *
 * numCmds:         Number of commands entered
 ***********************************************/
void processTokens(Vector* tokens, int numCmds)
{
    if(numCmds == 0)
        return;

    for(int i = 0; i < numCmds; i++) {
        if(tokens[i].capacity == 0)
            continue;

        char* cmd = tokens[i].arr[0];

        int fds[2] = {0};
        if(pipe(fds)) {
            perror("pipe");
            return;
        }

        pid_t id = fork();
        if(id == 0) { // Child
            if(numCmds > 1 && i < numCmds - 1) {
                dup2(fds[1], STDOUT_FILENO);
                close(fds[0]);
            }
            if(execvp(cmd, tokens[i].arr)) {
                perror(cmd);
                exit(1);
            }
        } else { // Parent
            if(numCmds > 1) {
                dup2(fds[0], STDIN_FILENO);
                close(fds[1]);
            }
            waitpid(id, NULL, 0);
        }
    }
}

/************************************************
 * checkBuiltinCmd: Check the user commands for
 *                  builtin shell commands
 *
 * tokens:          Array of vectors holding the
 *                  tokenized user input
 *
 * numCmds:         Number of commands entered
 *
 * return:          Is the current command a
 *                  shell builtin command
 ***********************************************/
bool checkBuiltinCmd(Vector* tokens, int numCmds)
{
    if(!tokens || numCmds == 0)
        return false;

    bool status;
    for(int i = 0; i < numCmds; i++) {
        status = false;
        if(strncmp(tokens[i].arr[0], "cd", sizeof("cd")) == 0) {
            static char prevDir[PATH_MAX];
            char temp[PATH_MAX] = {0};

            if(tokens[i].size == 1) {
                if(!getcwd(temp, PATH_MAX)) {
                    perror("getcwd");
                    return false;
                }
                char* homeDir = getenv("HOME");
                if(chdir(homeDir) != 0)
                    strlcpy(prevDir, temp, PATH_MAX);
            } else if(strncmp(tokens[i].arr[1], "-", sizeof("-")) == 0) {
                if(strnlen(prevDir, PATH_MAX) == 0) {
                    printf("cd: Previous Directory is not set\n");
                } else {
                    strlcpy(temp, prevDir, PATH_MAX);
                    getcwd(prevDir, PATH_MAX);
                    chdir(temp);
                }
            } else {
                if(!getcwd(temp, PATH_MAX)) {
                    perror("getcwd");
                    return false;
                } else if(chdir(tokens[i].arr[1]) == 0) {
                    strlcpy(prevDir, temp, PATH_MAX);
                } else {
                    printf("cd: %s is not a file or directory\n", tokens[i].arr[1]);
                }
            }
            status = true;
        } else if(strncmp(tokens[i].arr[0], "exit", sizeof("exit")) == 0) {
            if(tokens[i].size != 1) {
                printf("exit: Command takes no arguments\n");
                return false;
            }

            tcsetattr(STDIN_FILENO, TCSANOW, &old);
            exit(0);
        } else if(strncmp(tokens[i].arr[0], "exec", sizeof("exec")) == 0) {
            int wstatus = 0;
            int fds[2] = {0};
            if(numCmds != 1) {
                if(pipe(fds)) {
                    perror("pipe");
                    return false;
                }
            }

            pid_t pid = fork();
            switch(pid) {
                case -1:
                    close(fds[0]);
                    close(fds[1]);
                    break;
                case 0:
                    if(numCmds > 1 && i != numCmds - 1) {
                        dup2(fds[1], STDOUT_FILENO);
                        close(fds[0]);
                    }
                    execvp(tokens[i].arr[1], tokens[i].arr + 1);
                    break;
                default:
                    if(numCmds > 1) {
                        dup2(fds[0], STDIN_FILENO);
                        close(fds[1]);
                    }
                    waitpid(pid, &wstatus, 0);
                    status = true;
            }
        }
    }

    return status;
}

/************************************************
 * homeDirSubstitution: Check a string for a '~'
 *                      character in the first
 *                      position. If found
 *                      replace it with the users
 *                      home directory
 *
 * pInput:              Pointer to the string
 *                      which is being searched
 *
 * size:                Size of pInput string
 ***********************************************/
void homeDirSubstitution(char** pInput, size_t size)
{
    if(!pInput || size == 0)
        return;

    char* input = *pInput;
    char homeDir[PATH_MAX] = {0};

    if(input[0] == '~') {
        char uname[_SC_LOGIN_NAME_MAX] = {0};
        size_t j = 1;
        while(input[j] != '/' && j < strnlen(input, CMD_SIZE)) {
            uname[j-1] = input[j];
            j++;
        }

        if(strnlen(uname, _SC_LOGIN_NAME_MAX) != 0) {
            snprintf(homeDir, PATH_MAX, "/home/%s/", uname);
        } else {
            strlcpy(homeDir, getenv("HOME"), PATH_MAX);

            if(strnlen(homeDir, PATH_MAX) == 0) {
                struct passwd* user = getpwuid(getuid());
                strlcpy(homeDir, user->pw_dir, PATH_MAX);
            }
        }

        size_t strSize = strnlen(homeDir, PATH_MAX) + strnlen(input + j, CMD_SIZE) + 1;
        char* str = calloc(strSize, sizeof(char));
        if(!str)
            return;

        strlcpy(str, homeDir, strSize);
        strlcat(str, input + j, strSize);

        free(input);
        *pInput = str;
    }
}

/************************************************
 * checkRedirection:    Search the input tokens
 *                      for input or output
 *                      redirection, '<' or '>'
 *
 * tokens:              Vector of input tokens
 *
 * return:              Reports if any
 *                      redirection happened
 ***********************************************/
bool checkRedirection(Vector tokens)
{
    if(tokens.size == 0)
        return false;

    bool inputRedir = false, outputRedir = false;
    char inputFile[FILENAME_MAX] = {0};
    char outputFile[FILENAME_MAX] = {0};

    for(size_t i = 0; i < tokens.size; i++) {
        if(!inputRedir && strncmp(tokens.arr[i], "<", 2) == 0) { // Inut Redirection
            if(i + 1 < tokens.size) {
                strncpy(inputFile, tokens.arr[i+1], FILENAME_MAX);
                free(tokens.arr[i]);
                free(tokens.arr[i+1]);

                size_t j = i + 2;
                for(; j < tokens.size; j++)
                    tokens.arr[j-2] = tokens.arr[j];

                tokens.arr[j-1] = NULL;
                tokens.arr[j-2] = NULL;
                tokens.size -= 2;
                inputRedir = true;
            }
        } else if(!outputRedir && strncmp(tokens.arr[i], ">", 2) == 0) { // Output Redirection
            if(i + 1 < tokens.size) {
                strncpy(outputFile, tokens.arr[i+1], FILENAME_MAX);
                free(tokens.arr[i]);
                free(tokens.arr[i+1]);

                size_t j = i + 2;
                for(; j < tokens.size; j++)
                    tokens.arr[j-2] = tokens.arr[j];

                tokens.arr[j-1] = NULL;
                tokens.arr[j-2] = NULL;
                tokens.size -= 2;

                outputRedir = true;
            }
        }
    }

    if(inputRedir) {
        int fd = open(inputFile, O_RDONLY, 0666);
        if(fd)
            dup2(fd, STDIN_FILENO);
    }

    if(outputRedir) {
        int fd = open(outputFile, O_RDWR | O_CREAT, 0666);
        if(fd)
            dup2(fd, STDOUT_FILENO);
    }

    return inputRedir || outputRedir;
}

/************************************************
 * countPipes:  Counts the number of pipes in the
 *              input tokens
 *
 * tokens:      Vector of input tokens
 *
 * return:      Number of pipes in input
 ***********************************************/
int countPipes(Vector tokens)
{
    if(tokens.size == 0)
        return 0;

    int numPipes = 0;
    for(size_t i = 0; i < tokens.size; i++) {
        if(strncmp(tokens.arr[i], "|", 2) == 0)
            numPipes++;
    }

    return numPipes;
}

/************************************************
 * extractPath: Extract the path from input
 *              string. If no path is found the
 *              path defaults to cwd.
 *              Home directory substitution is
 *              performed on the path
 *
 * input:       String to search for a path
 *
 * inputSize:   Size of input buffer
 *
 * path:        Pointer to a buffer of size
 *              PATH_MAX, in which the found path
 *              will be stored
 ***********************************************/
void extractPath(char* input, int inputSize, char** path)
{
    if(!path || !input || inputSize == 0) {
        strcpy(*path, ".");
        return;
    }
    memset(*path, 0, PATH_MAX);

    int i = inputSize - 1;
    for(; i >= 0; i--) {
        if(input[i] == '/')
            break;
    }

    if(i == -1) {
        strncpy(*path, "./", 3);
    } else {
        strncat(*path, input, i + 1);
        homeDirSubstitution(path, i + 1);
    }
}

/************************************************
 * findAutofillStrings: Create a vector of files
 *                      and directories which
 *                      start with input
 *
 * input:               String to find matches
 *                      for
 *
 * size:                Size of input string
 *
 * path:                Path to search for
 *                      matches
 *
 * return:              Vector of all matches
 ***********************************************/
Vector findAutofillStrings(const char* input, size_t size, const char* path)
{
    DIR* dirp = opendir(path);
    if(!dirp || size == 0)
        return (Vector){0, 0, 0};

    Vector autofills = vectorInit(0);
    if(autofills.capacity == 0) {
        closedir(dirp);
        return autofills;
    }

    struct dirent* dent;
    while((dent = readdir(dirp))) {
        if(size > strnlen(dent->d_name, NAME_MAX))
            continue;

        bool isMatch = true;
        for(size_t i = 0; i < size; i++) {
            if(input[i] != dent->d_name[i]) {
                isMatch = false;
                break;
            }
        }

        if(isMatch) {
            // 256 is the size of d_name buffer
            size_t len = strnlen(dent->d_name, 256) + 2;
            char str[len];
            struct stat st = {0};

            if(dent->d_type == DT_LNK) {
                char temp[PATH_MAX] = {0};
                strlcpy(temp, path, PATH_MAX);
                strlcat(temp, dent->d_name, PATH_MAX);

                stat(temp, &st);
            }

            if(dent->d_type == DT_DIR || (st.st_mode & S_IFDIR)) {
                strlcpy(str, dent->d_name, len);
                strlcat(str, "/", len);
                vectorInsert(&autofills, str, len);
            } else {
                vectorInsert(&autofills, dent->d_name, strnlen(dent->d_name, NAME_MAX));
            }
        }
    }

    closedir(dirp);
    return autofills;
}

/**********************************************************
 * findLongestCommonPrefix: Find the longest prefix found
 *                          in all autofill strings
 *
 * buffer:                  Buffer which the longest common
 *                          substring is copied into
 *
 * size:                    Size of buffer
 *********************************************************/
void findLongestCommonPrefix(Vector autofills, char* buffer, size_t size)
{
    if(!buffer)
        return;

    size_t count;
    size_t min = INT32_MAX;
    for(size_t i = 0; i < autofills.size; i++) {
        size_t iSize = strnlen(autofills.arr[i], PATH_MAX);
        if(iSize < min)
            min = iSize;
    }

    size_t i = 0;
    for(; i < min; i++) {
        char c = autofills.arr[0][i];
        for(size_t j = 1; j < autofills.size; j++) {
            if(autofills.arr[j][i] != c) {
                goto findLongestCommonPrefixEnd;
            }
        }
    }

findLongestCommonPrefixEnd:
    count = size < i ? size : i;
    strncpy(buffer, autofills.arr[0], count);
}
