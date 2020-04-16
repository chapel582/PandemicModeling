/*
Questions to answer
What about herd immunity strategy where only the old or vulnerable stay in?
What happens if we stop social isolation now/very soon?
What if they aren't infecting anyone after they start exhibiting symptoms?
What happens if there's some reduction in transmission after infection but not 
What happens if people with "close contact" also isolate themselves?
	(before exhibiting symptoms)
What about essential workers? How many of them are there?
*/

/*
Assumptions
Encounter rate is constant. Not a function of number who have died
*/

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <windows.h>

int64_t GlobalPerformanceFrequency;

// NOTE: Performance inspection stuff
inline LARGE_INTEGER GetWallClock(void)
{
	LARGE_INTEGER Result;
	QueryPerformanceCounter(&Result);
	return Result;
}

inline float GetSecondsElapsed(LARGE_INTEGER Start, LARGE_INTEGER End)
{
	float Result;
	Result = (
		((float) (End.QuadPart - Start.QuadPart)) / 
		((float) GlobalPerformanceFrequency)
	);
	return Result;
}

// NOTE: need randoms > RAND_MAX
uint64_t Rand(uint64_t Mod)
{
	uint64_t Result = (
		(((uint64_t) (rand() % 0xFF)) << 56) |
		(((uint64_t) (rand() % 0xFF)) << 48) |
		(((uint64_t) (rand() % 0xFF)) << 40) |
		(((uint64_t) (rand() % 0xFF)) << 32) |
		(((uint64_t) (rand() % 0xFF)) << 24) |
		(((uint64_t) (rand() % 0xFF)) << 16) |
		(((uint64_t) (rand() % 0xFF)) << 8) |
		(((uint64_t) (rand() % 0xFF)))
	);
	return Result % Mod;
}

// NOTE: need randoms 0.0 < 1.0 
float RandUnity()
{
	return ((float) (rand() % RAND_MAX)) / ((float) RAND_MAX);
}

// NOTE: command line argument token handling
int ParseArg(
	char* Format, char* Argv[], int Index, char* Argument, void* Result
)
{
	if(strcmp(Argv[Index++], Argument) == 0)
	{
		int ScanResult = sscanf_s(Argv[Index], Format, Result);
		if(ScanResult != 1)
		{
			printf("Unable to parse %s for %s", Argv[Index], Argv[Index - 1]);
			return 1;
		}
		else
		{
			return 0;
		}
	}
	else
	{
		return 1;
	}
}

typedef enum state
{
	State_Susceptible,
	State_Infected,
	State_Recovered,
	State_Dead
} state;

typedef struct person
{
	state State;
	state NextState;
	int DaysInState; // NOTE: only tracked for IR states
} person;

void InitPerson(person* Person, state State)
{
	Person->State = State;
	Person->NextState = State;
	Person->DaysInState = 0; // NOTE: only tracked for IR states
}

typedef struct set_next_state_data
{
	float DeathRate;
	float EncounterRate;
	HANDLE TotalsMutex;
	int DaysSick;
	person* People;
	uint64_t StartAt;
	uint64_t EndAt;
	uint64_t OriginalPop;
	uint64_t* TotalSusceptible;
	uint64_t* TotalInfected;
	uint64_t* TotalRecovered;
	uint64_t* TotalDead;
} set_next_state_data;

DWORD WINAPI SetNextState(LPVOID LpParameter)
{
	set_next_state_data* Args = (set_next_state_data*) LpParameter;
	float DeathRate = Args->DeathRate;
	float EncounterRate = Args->EncounterRate;
	int EncounterRateInt = (int) EncounterRate;
	float EncounterRateFractional = (
		(float) EncounterRate - (float) EncounterRateInt
	);
	int DaysSick = Args->DaysSick;
	person* People = Args->People;
	uint64_t StartAt = Args->StartAt;
	uint64_t EndAt = Args->EndAt;
	uint64_t OriginalPop = Args->OriginalPop;
	uint64_t TotalSusceptible = 0;
	uint64_t TotalInfected = 0;
	uint64_t TotalRecovered = 0;
	uint64_t TotalDead = 0;
	srand(((int) time(NULL)) + (100 * GetCurrentThreadId()));
	
	// NOTE: allocate once per thread for speed
	person** ToInfect = (person**) malloc(
		((int) (EncounterRateInt) + 1) * sizeof(person*)
	);

	// NOTE: There may be some concern over race conditions here.
	// CONT: There shouldn't be any. We only modify next state of susceptibles
	// CONT: so there isn't a case where someone was goigng to recover and is 
	// CONT: then reinfected. 
	for(
		person* Person = &People[StartAt];
		Person < &People[EndAt];
		Person++
	)
	{
		if(Person->State == State_Susceptible)
		{
			TotalSusceptible++;
		}
		else if(Person->State == State_Infected)
		{
			TotalInfected++;

			if(Person->DaysInState > DaysSick)
			{
				float Value = RandUnity();
				if(Value < DeathRate)
				{
					Person->NextState = State_Dead;
				}
				else
				{
					Person->NextState = State_Recovered;
				}
			}
			else
			{					
				int LivingFound = 0;
				while(LivingFound < EncounterRateInt)
				{
					uint64_t RandomIndex = Rand(OriginalPop);
					person* RandomPerson = &People[RandomIndex];

					// NOTE: Make sure we found a living person
					if(RandomPerson->State != State_Dead)
					{
						ToInfect[LivingFound++] = RandomPerson;
					}
				}

				float Value = RandUnity();
				if(Value < EncounterRateFractional)
				{
					person* RandomPerson;
					bool RandomAlreadyPicked = false;
					do
					{
						uint64_t RandomIndex = Rand(OriginalPop);
						RandomPerson = &People[RandomIndex];
						
						// NOTE: Make sure we found a living, unrecovered person
						if(RandomPerson->State == State_Dead)
						{
							continue;
						}

						// NOTE: need to check that we haven't already picked this person
						for(
							int CheckIndex = 0;
							CheckIndex < LivingFound;
							CheckIndex++
						)
						{
							if(ToInfect[CheckIndex] == RandomPerson)
							{
								RandomAlreadyPicked = TRUE;
								break;
							}
						}
					} while(RandomAlreadyPicked);
					ToInfect[LivingFound++] = RandomPerson;
				}

				for(
					int ToInfectIndex = 0;
					ToInfectIndex < LivingFound;
					ToInfectIndex++
				)
				{
					person* PersonToInfect = ToInfect[ToInfectIndex];
					if(PersonToInfect->State == State_Susceptible)
					{
						PersonToInfect->NextState = State_Infected;							
					}
				}
			}
		}
		else if(Person->State == State_Recovered)
		{
			TotalRecovered++;
		}
		else if(Person->State == State_Dead)
		{
			TotalDead++;
		}
	}

	free(ToInfect);

	WaitForSingleObject(Args->TotalsMutex, INFINITE);
	*Args->TotalSusceptible += TotalSusceptible;
	*Args->TotalInfected += TotalInfected;
	*Args->TotalRecovered += TotalRecovered;
	*Args->TotalDead += TotalDead;
	ReleaseMutex(Args->TotalsMutex);
	return 0;
}

// NOTE: here's some more info on entry points & Windows
// CONT: https://docs.microsoft.com/en-us/cpp/cpp/main-function-command-line-args?view=vs-2019
int main(
	int Argc,
	char* Argv[],
	char* Envp[]
)
{
	LARGE_INTEGER PerformanceFrequency;
	QueryPerformanceFrequency(&PerformanceFrequency);
	GlobalPerformanceFrequency = PerformanceFrequency.QuadPart;

	// NOTE: PARAMETERS
	// TODO: add command line arguments
	// NOTE: the average number of people encountered by a person each day in 
	// NOTE: a way that would tranfer the virus. 
	// NOTE: fractional means that there's a chance the person doesn't get it
	float EncounterRate = 0.25;
	// NOTE: the average time in days to recover
	int DaysSick = 14;
	// NOTE: how many of the infected die 
	// NOTE: default based on S. Korea's COVID19 numbers as of April 15, 2020
	// NOTE: 225 / 10591
	float DeathRate = 0.02f;
	// TODO: give an option for making the death rate a function of hospital capacity
	// TOOD: give an option for people becoming reinfected (maybe after a certain amount of time) 

	// NOTE: initial conditions
	uint64_t SusceptiblePop = 999999;
	uint64_t InfectedPop = 1;
	uint64_t RecoveredPop = 0;
	uint64_t OriginalPop = SusceptiblePop + InfectedPop + RecoveredPop;

	// NOTE: Other arguments
	uint32_t SimulationDays = 365;
	int MaxThreads = 4;

	// NOTE: handle command line arguments
	for(int ArgumentIndex = 0; ArgumentIndex < Argc; ArgumentIndex++)
	{		
		if(
			ParseArg(
				"%f", Argv, ArgumentIndex, "--encounter", &EncounterRate
			) == 0
		)
		{
		}
		else if(
			ParseArg(
				"%u", Argv, ArgumentIndex, "--dayssick", &DaysSick
			) == 0
		)
		{
		}
		else if(
			ParseArg(
				"%f", Argv, ArgumentIndex, "--death", &DeathRate
			) == 0
		)
		{
		}
		else if(
			ParseArg(
				"%llu", Argv, ArgumentIndex, "--susceptible", &SusceptiblePop
			) == 0
		)
		{
		}
		else if(
			ParseArg(
				"%llu", Argv, ArgumentIndex, "--infected", &InfectedPop
			) == 0
		)
		{
		}
		else if(
			ParseArg(
				"%llu", Argv, ArgumentIndex, "--recovered", &RecoveredPop
			) == 0
		)
		{
		}
		else if(
			ParseArg(
				"%u", Argv, ArgumentIndex, "--simdays", &SimulationDays
			) == 0
		)
		{
		}
		else if(
			ParseArg(
				"%u", Argv, ArgumentIndex, "--threads", &MaxThreads
			) == 0
		)
		{
		}
		else if(strcmp(Argv[ArgumentIndex], "--help") == 0)
		{
			printf("--encounter <encounter>\n");
			printf("\tFloat. The average number of people an infected person will infect each day. Default: 0.25\n");
			printf("--dayssick <dayssick>\n");
			printf("\tInt. The number of days a person remains sick. Default: 14\n");
			printf("--death <death>\n");
			printf("\tFloat. The likelihood that someone dies from the illness. Default: 0.01\n");
			printf("--susceptible <susceptible>\n");
			printf("\tInt. Number of people not immune to the disease. Default 999999\n");
			printf("--infected <infected>\n");
			printf("\tInt. Number of people infected. Default: 1");
			printf("--recovered <recovered>\n");
			printf("\tInt. Number of people immune to the disease. Default: 0");
			printf("--simdays <simdays>\n");
			printf("\tInt. Number of days to simulate. Default: 365");
			printf("--threads <threads>\n");
			printf("\tInt. Number of threads to use. Default: 4");
			return 0;
		}
	}

	// NOTE: Susceptible and Infected need updates
	// NOTE: currently People mem block is never freed 
	// NOTE: this is an OK assumption since it's basically used until the death 
	// NOTE: of the program
	person* People = (person*) malloc(OriginalPop * sizeof(person));
	uint64_t PersonIndex;
	for(PersonIndex = 0; PersonIndex < SusceptiblePop; PersonIndex++)
	{
		person* Person = &People[PersonIndex];
		InitPerson(Person, State_Susceptible);
	}
	for(; PersonIndex < (SusceptiblePop + InfectedPop); PersonIndex++)
	{
		person* Person = &People[PersonIndex];
		InitPerson(Person, State_Infected);
	}
	for(; PersonIndex < OriginalPop; PersonIndex++)
	{
		person* Person = &People[PersonIndex];
		InitPerson(Person, State_Recovered);	
	}

	// NOTE: These also never get deallocated. No need
	HANDLE* ThreadHandles = (HANDLE*) malloc(MaxThreads * sizeof(HANDLE));
	set_next_state_data* ArgsArray = (set_next_state_data*) malloc(
		MaxThreads * sizeof(set_next_state_data)
	);
	HANDLE TotalsMutex = CreateMutexA(
		NULL, // NOTE: no need to inherit mutexes
		FALSE, // NOTE: whether we take initial ownership
		NULL // NOTE: name of mutex
	);
	uint64_t ThreadDivision = (uint64_t) (
		(float) OriginalPop / (float) MaxThreads
	);

	LARGE_INTEGER Start = GetWallClock();
	for(uint32_t Day = 0; Day < SimulationDays; Day++)
	{
		uint64_t TotalSusceptible = 0;
		uint64_t TotalInfected = 0;
		uint64_t TotalRecovered = 0;
		uint64_t TotalDead = 0;

		// TODO: see if these loops need parallelization for large values
		for(
			PersonIndex = 0;
			PersonIndex < OriginalPop;
			PersonIndex++
		)
		{
			person* Person = &People[PersonIndex]; 
			if(Person->State != Person->NextState)
			{
				// TODO: track new cases here
				Person->State = Person->NextState;
				Person->NextState = Person->State;
				Person->DaysInState = 0;	
			}
			Person->DaysInState++;
		}

		uint64_t LastEndAt;
		for(int ThreadIndex = 0; ThreadIndex < MaxThreads; ThreadIndex++)
		{
			set_next_state_data* Args = &ArgsArray[ThreadIndex];
			Args->DeathRate = DeathRate;
			Args->TotalsMutex = TotalsMutex;
			Args->EncounterRate = EncounterRate;
			Args->DaysSick = DaysSick;
			Args->People = People;
			Args->StartAt = ThreadIndex * ThreadDivision; 
			if(ThreadIndex == (MaxThreads - 1))
			{
				Args->EndAt = OriginalPop;
			}
			else
			{
				Args->EndAt = (ThreadIndex + 1) * ThreadDivision;
			}
			LastEndAt = Args->EndAt;
			Args->OriginalPop = OriginalPop;
			Args->TotalSusceptible = &TotalSusceptible;
			Args->TotalInfected = &TotalInfected;
			Args->TotalRecovered = &TotalRecovered;
			Args->TotalDead = &TotalDead;

			DWORD ThreadId;
			ThreadHandles[ThreadIndex] = CreateThread( 
				NULL, // default security attributes
				0, // use default stack size  
				SetNextState, // thread function name
				Args, // argument to thread function 
				0, // use default creation flags 
				&ThreadId // returns the thread identifier
			);
			if(ThreadHandles[ThreadIndex] == NULL)
			{
				printf("Unable to create thread %d", ThreadIndex);
				return 1;
			}
		}
		WaitForMultipleObjects(MaxThreads, ThreadHandles, TRUE, INFINITE);

		printf(
			"%u, %llu, %llu, %llu, %llu\n",
			Day + 1,
			TotalSusceptible,
			TotalInfected,
			TotalRecovered,
			TotalDead
		);
		// TODO: write out to a csv
	}

	LARGE_INTEGER End = GetWallClock();

	float SecondsElapsed = GetSecondsElapsed(Start, End);
	printf("Time to run %f\n", SecondsElapsed);
	fflush(stdout);
	return 0;
}