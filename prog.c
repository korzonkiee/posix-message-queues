#define _GNU_SOURCE
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <mqueue.h>

#define LIFE_SPAN 10
#define MAX_NUM 10
#define MAX_PID_LENGTH 10
#define MAXMSG 10
#define MAX_MSG_LENGTH 20
#define NEIGHBOURS_LIMIT 5
#define MAX_MSG_SIZE 30


#define ERR(source) (fprintf(stderr,"%s:%d\n",__FILE__,__LINE__),\
perror(source),kill(0,SIGKILL),\
exit(EXIT_FAILURE))

typedef enum MessageType
{
	Registration = 0,
	PlainMessage = 1

} MessageType;

typedef struct Message
{
	MessageType type;
	char senderPid[MAX_PID_LENGTH];
	char content[MAX_MSG_LENGTH];
} Message;

typedef struct Neighbour
{
	char pid[MAX_PID_LENGTH];
	mqd_t queue;

} Neighbour;

typedef struct NeighboursCollection
{
	Neighbour neighbours[NEIGHBOURS_LIMIT];
	int size;
} NeighboursCollection;

void prepend(char* s, const char* t)
{
	size_t len = strlen(t);
	size_t i;

	memmove(s + len, s, strlen(s) + 1);

	for (i = 0; i < len; ++i)
	{
		s[i] = t[i];
	}
}

void closeQueue(mqd_t *queue, char *queueId)
{
	if (mq_close(*queue))
	{
		ERR("Error closing queue");
	}

	char *tempName = (char*) malloc(sizeof(char) * strlen(queueId));
	strcpy(tempName, queueId);
	prepend(tempName, "/");

	if (mq_unlink(tempName))
	{
		ERR("Error unlinkin queue");
	}
}

void initializeQueue(mqd_t *queue, char *queueId)
{
	struct mq_attr attr;
	attr.mq_maxmsg = 10;
	attr.mq_msgsize = MAX_MSG_SIZE;

	char *tempName = (char*) malloc(sizeof(char) * strlen(queueId));
	strcpy(tempName, queueId);
	prepend(tempName, "/");

	*queue = TEMP_FAILURE_RETRY(mq_open(
		tempName,
		O_RDWR | O_NONBLOCK | O_CREAT,
		0600, &attr
		));

	free(tempName);
	if (*queue == (mqd_t)-1)
	{
		ERR("Error opening queue");
	}
}

void intHandler(int dummy)
{
	int i;
	for (i = 0; i < neighboursSize; i++)
	{
		mqd_t queue;
		initializeQueue(&queue, neighbours[i]);

		printf("\nSending terminate singal to [%s]", neighbours[i]);
		long pid = strtol(neighbours[i], NULL, 10);
		if (kill(pid, SIGINT))
		{
			ERR("Error sending kill to process\n");
		}

		closeQueue(&queue, neighbours[i]);
	}
	printf("\nTerminating...\n");
	exit(EXIT_SUCCESS);
}


void sethandler( void (*f)(int, siginfo_t*, void*), int sigNo)
{
	struct sigaction act;
	memset(&act, 0, sizeof(struct sigaction));
	act.sa_sigaction = f;
	act.sa_flags=SA_SIGINFO;
	if (-1==sigaction(sigNo, &act, NULL))
	{
		ERR("sigaction");
	}
}

int countDigits(long number)
{
	int count = 0;

	while(number != 0)
	{
		number /= 10;
		++count;
	}

	return count;
}

void parseNeighbourPidFromArgs(char **neighbourPid, char **argv)
{
	if (argv[1] != NULL && strlen(argv[1]) > 0)
	{
		*neighbourPid = (char*) malloc(sizeof(char) * strlen(argv[1]));
		strcpy(*neighbourPid, argv[1]);
	}
}

void createCurrentProcessPidString(char **myPid)
{
	long pid = (long) getpid();
	*myPid = (char*) malloc(sizeof(char) * countDigits(pid));
	sprintf(*myPid, "%ld", pid);
}

void addNeighbour(char *neighbourPid)
{
	neighboursSize++;
	if (neighbours == NULL)
	{
		neighbours = (char**) malloc(sizeof(char*) * neighboursSize);
	}
	else
	{	
		neighbours = (char**) realloc(neighbours, sizeof(char*) * neighboursSize);
	}
	
	neighbours[neighboursSize - 1] = (char*) malloc(sizeof(char) * strlen(neighbourPid));
	strcpy(neighbours[neighboursSize-1], neighbourPid);
}

void printNeighbours()
{
	printf("My neighbours:\n");
	int i;
	for (i = 0; i < neighboursSize; i++)
	{
		printf("%d. [%s]\n",i + 1, neighbours[i]);
	}
	printf("\n");
}

void printMessage(Message message)
{
	printf("Message:\n    Type: %d\n    Sender: [%s]\n    Content: %s\n\n",
		message.type, message.senderPid, message.content);
}

void subscribeToMessageQueue(mqd_t *queue)
{
	static struct sigevent notification;
	notification.sigev_notify = SIGEV_SIGNAL;
	notification.sigev_signo = SIGRTMIN;
	notification.sigev_value.sival_ptr=queue;
	if(mq_notify(*queue, &notification) < 0)
	{
		ERR("mq_notify");
	}
	printf("Subscribed successfully\n");
}

void mq_handler(int sig, siginfo_t *info, void *p)
{
	printf("Msg received.\n");
	mqd_t *pin;
	pin = (mqd_t *)info->si_value.sival_ptr;
	subscribeToMessageQueue(pin);

	for(;;)
	{
		int bytesRead;
		Message *message = (Message*) malloc(sizeof(Message));

		if((bytesRead = mq_receive(*pin, (char *) message, MAX_MSG_SIZE,
			NULL)) < 1)
		{
			if(errno==EAGAIN) break;
			else ERR("mq_receive");
		}
		else
		{
			switch (message->type)
			{
				case PlainMessage:
					printf("PlainMessage\n");
					printMessage(*message);
					break;
				case Registration:
					printf("RegistrationRequest\n");
					addNeighbour(message->senderPid);
					printNeighbours();
					break;
				default:
					printf("Undefined message type\n");
					break;
			}
			// printf("Size: %lu\n", sizeof(MessageType) + sizeof(char) * MAX_PID_LENGTH + sizeof(char) * MAX_MSG_LENGTH);
			// printf("%s\n", message);
			// printf("Bytes read: %d\n", bytesRead);

			// if (neighboursSize >= NEIGHBOURS_LIMIT)
			// {
			// 	printf("Maximal number of clients reached!\n");
			// 	return;
			// }
			// addNeighbour(message);
			// printNeighbours();			
		}
	}
}

void sendRegistrationRequest(char *senderPid, char *queueName)
{
	mqd_t queue;
	initializeQueue(&queue, queueName);

	Message message;
	message.type = Registration;
	strcpy(message.senderPid, senderPid);

	if(TEMP_FAILURE_RETRY(mq_send(queue, (const char*) &message, MAX_MSG_SIZE, SIGRTMIN)))
	{
		ERR("mq_send");
	}
}

void sendMessage(char *messageContent, char *neighbourPid, char *senderPid)
{	
	Message message;
	message.type = PlainMessage;
	strcpy(message.senderPid, senderPid);
	strcpy(message.content, messageContent);

	printMessage(message);

	if(TEMP_FAILURE_RETRY(mq_send(queue, (const char*) &message, MAX_MSG_SIZE, SIGRTMIN)))
	{
		ERR("mq_send");
	}
}

void readDataAndSendMessage(char *currentProcessPid) {
	char line[MAX_PID_LENGTH + MAX_MSG_LENGTH + 2];
	char tempPid[MAX_PID_LENGTH];
	char stringData[MAX_MSG_LENGTH + 1];
	while (1)
	{
		printf("Reading...\n");
		if (read(STDIN_FILENO, line, MAX_PID_LENGTH + MAX_MSG_LENGTH + 2))
		{
			if (errno == EINTR) {
				printf("Reading interrputed.\n");
				continue;
			}
		}
		if (sscanf(line, "%s %20s", tempPid, stringData) != 2) {
			printf("Invalid input data.\n");
			continue;
		}

		sendMessage(stringData, tempPid, currentProcessPid);
	}
}

int main(int argc, char** argv)
{
	
	char *neighbourPid = NULL;
	char *currentProcessPid;
	
	parseNeighbourPidFromArgs(&neighbourPid, argv);
	createCurrentProcessPidString(&currentProcessPid);

	printf("PID: [%s]\n\n", currentProcessPid);

	mqd_t queue;
	initializeQueue(&queue, currentProcessPid);

	signal(SIGINT, intHandler);
	sethandler(mq_handler, SIGRTMIN);
	subscribeToMessageQueue(&queue);

	if (neighbourPid != NULL)
	{
		sendRegistrationRequest(currentProcessPid, neighbourPid);
	}
	
	readDataAndSendMessage(currentProcessPid);

	closeQueue(&queue, currentProcessPid);
	return EXIT_SUCCESS;
}
