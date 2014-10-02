#include <iostream>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <vector>
#include <stdio.h>
#include <errno.h>
#include <fstream>

void interactiveLoop();
void executeFile(std::string fileName);

void parseCommand(std::string command);
void executeExpression(std::vector<std::string> expressionBuf);

bool runExternalProgram(std::vector<std::string> expressionBuf);
bool runProgFromPath(std::vector<std::string> expressionBuf);

void createProcess(std::string progName, std::vector<std::string> expressionBuf);
void executePipes(std::vector<std::vector<std::string> > pipeBuf);

bool progExists(std::string progName);
bool checkPath(std::string progName);

void cd(std::string pathName);

std::string cwdName; 	// Current working directory name

int main(int argc, char ** argv)
{
	if(argc == 1)
	{
		interactiveLoop();
	}
	else if(argc == 2)
	{
		executeFile(argv[1]);
	}
	
	return 0;
}

// Interactively reads commands

void interactiveLoop()
{	
	// Get user
	std::string userName = getenv("USER");
	
	// Get host
	char hostName[25];
	gethostname(hostName, 25);
	
	// Prefix for each line in terminal
	std::string prefix = "[" + userName + "]::";
	
	// Start at user home
	cwdName = "/home/" + userName;
	
	if(chdir(cwdName.c_str()) == -1)	// Change to home directory
	{
		printf("Error opening home directory\n");
		return;
	}
	
	cwdName = userName;
	
	while(true)
	{
		std::cout << prefix << cwdName << "> ";
		
		std::string inputBuf;	// Holds each input string
		std::getline(std::cin, inputBuf);
		
		if(inputBuf.length() != 0)
		{
			parseCommand(inputBuf);
		}
	}
}

// Executes script file

void executeFile(std::string fileName)
{
	if(fileName.substr(fileName.find_last_of(".") + 1) == "sh")	// Check for correct file extension
	{
    		std::ifstream inFile(fileName.c_str());
		std::string line;
		while(std::getline(inFile, line))
		{
			parseCommand(line);
		}	
  	}
  	else
  	{
  		std::cout << "Error: input file must use the \'.sh\' extension\n";
  	}
}

// Parses and executes command

void parseCommand(std::string command)
{
	std::vector<std::string> expressionBuf;	// Holds multiple tokens to form an expression
	std::vector<std::vector<std::string> > pipeBuf;		// Holds multiple expressions to be piped together
	
	int saved_stdout;	// For saving stdout descriptor after redirect
	bool redirect = false;	// If stdout has been redirected

	char cmdString[1024];
	strcpy(cmdString, command.c_str());
	char * token = strtok(cmdString, " \n");	// Seperate commands by space and endline
	
	while(token != NULL)
	{
		if(strcmp(token, "|") == 0)	// Pipe between processes
		{
			if(expressionBuf.size() > 0)	// Check that there are tokens before pipe
			{
				pipeBuf.push_back(expressionBuf);
				expressionBuf.clear();
			}
			else
			{
				std::cout << "Error: no command given as pipe input" << std::endl;
				return;
			}
		}
		else if((strcmp(token, ">") == 0) || (strcmp(token, ">>") == 0))	// Redirect output to file
		{
			bool overwrite = (strcmp(token, ">") == 0);	// File should be overwritten
			bool append = (!overwrite);	// File should be appended to
			
			char * fileName = strtok(NULL, " \n");	// Get next token for filename
			if(fileName == NULL)	// If no tokens after redirect
			{
				std::cout << "Error: no filename given after redirect character" << std::endl;
				return;
			}
			else if(strtok(NULL, " \n") != NULL)	// If multiple tokens after redirect
			{
				std::cout << "Error: too many arguments after redirect character" << std::endl;
				return;
			}
			else
			{
				FILE * outFile;
				if(overwrite)	// Determine whether file should be overwritten or appended to
				{
					outFile = fopen(fileName, "w");
				}
				else if(append)
				{
					outFile = fopen(fileName, "a+");
				}
				
				if(outFile == NULL)
				{
					std::cout << "No file with the name \'" << fileName << "\' exists" << std::endl;
					return;
				}
				else
				{
					saved_stdout = dup(1);	// Save stdout descriptor
					if(dup2(fileno(outFile), 1) < 0)	// Redirect stdout to file descriptor
					{
						perror("dup2");
						_exit(EXIT_FAILURE);
					}
					else
					{
						redirect = true;
						fclose(outFile);
						break;
					}
				}
			}
		}
		else
		{
			expressionBuf.push_back(token);		// Add each token to expressionBuf
		}
		token = strtok(NULL, " \n");
	}
	if(expressionBuf.size() <= 0)	// No tokens after pipe
	{
		std::cout << "Error: no command after pipe character" << std::endl;
		return;
	}
	else
	{
		pipeBuf.push_back(expressionBuf);
	}
	
	if(pipeBuf.size() > 1)
	{
		executePipes(pipeBuf);
	}
	else
	{
		executeExpression(expressionBuf);
	}
	
	if(redirect)
	{
		dup2(saved_stdout, 1);		// Restore stdout after redirect
	}
}

// Executes a single expression

void executeExpression(std::vector<std::string> expressionBuf)
{
	if(expressionBuf[0] == "cd")	// Change directory command
	{
		if(expressionBuf.size() == 2)
		{
			cd(expressionBuf[1]);
		}
		else
		{
			printf("cd: invalid arguments\n");
		}
	}
	else if(expressionBuf[0] == "exit")	// Exit shell command
	{
		if(expressionBuf.size() == 1)
		{
			exit(0);
		}
		else
		{
			printf("exit takes no arguments\n");
		}
	}
	else	// Not a built in command
	{
		if(!(runExternalProgram(expressionBuf)))	// Try to run an external program
		{
			std::cout << "No program with the name \'" << expressionBuf[0] << "\' exists" << std::endl;
		}
	}
}

// Runs an external program (duh)

bool runExternalProgram(std::vector<std::string> expressionBuf)
{
	std::string progName = expressionBuf[0];
	if(checkPath(expressionBuf[0]))	// Check PATH variable directories
	{
		pid_t pid = fork();	// Spawn child process to run command
  	  	int status;
 	  	if(pid == 0)
		{
  	  		runProgFromPath(expressionBuf);
  	     	}
   	     	else
   		{
       			(void)waitpid(pid, &status, 0);
   		}
		return true;
	}
	else if(progExists(progName))
	{
		pid_t pid = fork();
  	  	int status;
 	  	if(pid == 0)
		{
  	  		createProcess(progName, expressionBuf);
  	     	}
   	     	else
   		{
       			(void)waitpid(pid, &status, 0);
   		}
   		return true;
	}
}

// Creates a process using PATH variable as program location

bool runProgFromPath(std::vector<std::string> expressionBuf)
{
	char * pathText = getenv("PATH");
	char * tokPathText = new char[strlen(pathText) + 1];
	strcpy(tokPathText, pathText);
	
	if(pathText == NULL)	// PATH variable can't be accessed
	{
		std::cout << "Error accessing PATH variable" << std::endl;
		return false;
	}
	else
	{
		char * dir = strtok(tokPathText, ":");		// Split PATH by ':'
		while(dir != NULL)
		{
			std::string pathVariableProg = dir;	// Name of program to be run
			pathVariableProg += "/" + expressionBuf[0];		// Append filename
			
			if(progExists(pathVariableProg))
			{
   			     	createProcess(pathVariableProg, expressionBuf);
   			     	return true;
			}
				
			dir = strtok(NULL, ":");
		}
	}
}

// Switches the process to appropriate program with arguments

void createProcess(std::string progName, std::vector<std::string> expressionBuf)
{
	char ** args = new char *[expressionBuf.size() + 1];	// Arguments to be passed to process
	for(int i = 0; i < expressionBuf.size(); i++)
	{
		args[i] = new char[expressionBuf[i].length()];
		strcpy(args[i], expressionBuf[i].c_str());
	}
	args[expressionBuf.size()] = NULL;	// Last arg has to be NULL
	
	execv(progName.c_str(), args);
	_exit(EXIT_SUCCESS);
}

// Executes multiple processes and pipes output together

void executePipes(std::vector<std::vector<std::string> > pipeBuf)
{
	// Check that all programs exist
	
	for(int i = 0; i < pipeBuf.size(); i++)
	{
		if(checkPath(pipeBuf[i][0]))
		{
			// Do nothing
		}
		else if(!progExists(pipeBuf[i][0]))
		{
			std::cout << "No program with the name \'" << pipeBuf[i][0] << "\' exists" << std::endl;
			return;
		}
	}

	pid_t pid;
	
	int numProcesses = pipeBuf.size();	// Number of processes to be piped together
	int numPipes = pipeBuf.size() - 1;
	int numPipeStreams = numPipes * 2;	// Number of descriptors
	
	int pipes[numPipeStreams];	// File descriptors
	
	// Create appropriate pipes
	
	for(int i = 0; i < numPipes; i++)
	{
		if(pipe(pipes + i * 2) < 0)
		{
			perror("pipe");
			_exit(EXIT_FAILURE);
		}
	}
	
	// Link pipes together
	
	for(int i = 0; i < pipeBuf.size(); i++)
	{
		pid = fork();
		if(pid == 0)	// In child process
		{
			if(i != 0)	// Get input from pipe if not first process
			{
				if(dup2(pipes[(i - 1) * 2], 0) < 0)
				{
					perror("dup2");
					_exit(EXIT_FAILURE);
				}
			}
			if(i != pipeBuf.size() - 1)	// Send output to pipe if not last process
			{
				if(dup2(pipes[i * 2 + 1], 1) < 0)
				{
					perror("dup2");
					_exit(EXIT_FAILURE);
				}
			}
			
			// Close all pipes
			
			for(int j = 0; j < numPipeStreams; j++)
			{
				close(pipes[j]);
			}
			
			if(runProgFromPath(pipeBuf[i]))		// Try to run program using PATH variable
			{
			
			}
			else		// If not in PATH, using local program
			{
				createProcess(pipeBuf[i][0], pipeBuf[i]);
			}
		}
		else if(pid < 0)	// Error forking process
		{
			perror("fork");
			_exit(EXIT_FAILURE);
		}
	}
	for(int i = 0; i < numPipeStreams; i++)		// Close all pipes after executing child processes
	{
		close(pipes[i]);
	}
	while (pid = waitpid(-1, NULL, 0))	// Wait for all children to kill themselves lol
	{
   		if (errno == ECHILD)
   		{
   			break;
   		}
	}
}

// Checks if program exists

bool progExists(std::string progName)
{
	if(access(progName.c_str(), F_OK) != -1)
	{
		struct stat s;
		if(stat(progName.c_str(), &s) == 0)	// Check if object is a file or directory
		{
	   		if(s.st_mode & S_IFDIR)		// Directory
	   		{
	   			return false;
	   	 	}
	   	 	else if(s.st_mode & S_IFREG)	// File
	  	  	{
   			     	return true;
  		 	}
		}
	}
	else	// Program doesn't exist
	{
		return false;
	}
}

// Checks if a program is in a directory in the PATH variable

bool checkPath(std::string progName)
{
	char * pathText = getenv("PATH");
	char * tokPathText = new char[strlen(pathText) + 1];
	strcpy(tokPathText, pathText);
	
	if(pathText == NULL)	// PATH variable can't be accessed
	{
		return false;
	}
	else
	{
		char * dir = strtok(tokPathText, ":");		// Split PATH by ':'
		while(dir != NULL)
		{
			std::string pathVariableProg = dir;	// Name of program to be run
			pathVariableProg += "/" + progName;		// Append filename
			
			if(progExists(pathVariableProg))
			{
				return true;
			}
				
			dir = strtok(NULL, ":");
		}
	}
	return false;
}

// Built in command to change directory

void cd(std::string pathName)
{
	if(chdir(pathName.c_str()) == 0)	// Check if path exists
	{
		char temp[1024];
       		if (getcwd(temp, sizeof(temp)) != NULL)
       		{
       			cwdName = temp;
       			if(cwdName != "/")
       			{
       				cwdName = cwdName.substr(cwdName.find_last_of('/') + 1);	// Only print local directory name
       			}
       		} 
	}
	else
	{
		std::cout << "cd: No directory with the name \'" << pathName << "\' exists" << std::endl;
	}
}
