#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <windows.h>

int64_t GlobalPerformanceFrequency;
uint64_t GlobalInfectedCount;
uint64_t GlobalVentilatorCount; 

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

typedef enum recover_type
{
	RecoverType_Probability,
	RecoverType_Delay
}recover_type;

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
	recover_type RecoverType;
	int DaysInState;
} person;

void InitPerson(person* Person, state State, recover_type RecoverType)
{
	Person->State = State;
	Person->NextState = State;
	Person->RecoverType = RecoverType;
	Person->DaysInState = 0;
}

typedef struct set_next_state_data
{
	float BelowCapacityDeathRate;
	float AboveCapacityDeathRate;
	float EncounterRate;
	HANDLE TotalsMutex;
	int DaysSick;
	int DaysImmune;
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
	float BelowCapacityDeathRate = Args->BelowCapacityDeathRate;
	float AboveCapacityDeathRate = Args->AboveCapacityDeathRate;
	float EncounterRate = Args->EncounterRate;
	int EncounterRateInt = (int) EncounterRate;
	float EncounterRateFractional = (
		(float) EncounterRate - (float) EncounterRateInt
	);
	int DaysSick = Args->DaysSick;
	int DaysImmune = Args->DaysImmune;
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

			// NOTE: this is here to help with initialized infected having
			// NOTE: probabilistic recovery instead of delay recovery
			bool ShouldRecover;
			if(Person->RecoverType == RecoverType_Probability)
			{
				float RecoverCheck = RandUnity();
				ShouldRecover = RecoverCheck < (1.0f / float(DaysSick));	
			}
			else
			{
				ShouldRecover = Person->DaysInState > DaysSick;
			}

			if(ShouldRecover)
			{
				float Value = RandUnity();
				float DeathRate;
				// NOTE: the 0.05 GlobalInfectedCount is based on New York's
				// CONT: ratio between infected (~200000) to their ventilators
				// CONT: (~10000) resulting in a higher fatality rate
				// CONT: source https://www.usatoday.com/story/news/factcheck/2020/04/01/fact-check-does-new-york-have-stockpile-unneeded-ventilators/5097170002/
				if((0.05 * GlobalInfectedCount) > GlobalVentilatorCount)
				{
					DeathRate = AboveCapacityDeathRate;
				}
				else
				{
					DeathRate = BelowCapacityDeathRate;
				}
				if(Value < DeathRate)
				{
					Person->NextState = State_Dead;
				}
				else
				{
					Person->NextState = State_Recovered;
					Person->RecoverType = RecoverType_Delay;
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
			if(Person->DaysInState > DaysImmune)
			{
				Person->NextState = State_Susceptible;
			}
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
	// NOTE: the average number of people encountered by a person each day in 
	// NOTE: a way that would tranfer the virus. 
	// NOTE: fractional means that there's a chance the person doesn't get it
	float EncounterRate = 0.25;
	// NOTE: the average time in days to recover
	int DaysSick = 14;
	// NOTE: how many of the infected die when ventilators are available
	// NOTE: default based on S. Korea's COVID19 numbers as of April 15, 2020
	// CONT: 225 / 10591
	float BelowCapacityDeathRate = 0.02f;
	// NOTE: how many of the infected die when ventilators aren't available
	// NOTE: this is based on NY's data as of April 15, 2020
	// NOTE: 213779 cases and 11586 deaths
	float AboveCapacityDeathRate = 0.055f;
	// NOTE: ventilator estimate based on # in the US https://www.washingtonpost.com/health/2020/03/13/coronavirus-numbers-we-really-should-be-worried-about/
	GlobalVentilatorCount = 170000;
	// NOTE: The percentage of people infected who need a ventilator

	// NOTE: initial conditions
	uint64_t SusceptiblePop = 999999;
	uint64_t InfectedPop = 1;
	uint64_t RecoveredPop = 0;

	// NOTE: Other arguments
	uint32_t SimulationDays = 365;
	// NOTE: The # of days you keep your immunity
	// NOTE: By default, it's "infinite" by eing SimulationDays + 1
	int DaysImmune = -1;
	int MaxThreads = 4;

	// NOTE: handle command line arguments
	bool SetDaysImmune = FALSE;
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
				"%f", Argv, ArgumentIndex, "--death", &BelowCapacityDeathRate
			) == 0
		)
		{
		}
		else if(
			ParseArg(
				"%f", Argv, ArgumentIndex, "--abovedeath", &AboveCapacityDeathRate
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
		else if(
			ParseArg(
				"%u", Argv, ArgumentIndex, "--daysimmune", &DaysImmune
			) == 0
		)
		{
			SetDaysImmune = TRUE;
		}
		else if(
			ParseArg(
				"%u",
				Argv,
				ArgumentIndex,
				"--ventilators",
				&GlobalVentilatorCount
			) == 0
		)
		{
		}
		else if(strcmp(Argv[ArgumentIndex], "--help") == 0)
		{
			printf("--encounter <encounter>\n");
			printf(
				"\tFloat. The average number of people an infected person will infect each day. Default: 0.25\n"
			);
			printf("--dayssick <dayssick>\n");
			printf("\tInt. The number of days a person remains sick. Default: 14\n");
			printf("--death <death>\n");
			printf("\tFloat. The likelihood that someone dies when there are enough ventilators. Default: 0.02\n");
			printf("--abovedeath <abovedeath>\n");
			printf("\tFloat. The likelihood that someone dies from the illness when there aren't enough ventilators Default: 0.055\n");
			printf("--susceptible <susceptible>\n");
			printf("\tInt. Number of people not immune to the disease. Default 999999\n");
			printf("--infected <infected>\n");
			printf("\tInt. Number of people infected. Default: 1\n");
			printf("--recovered <recovered>\n");
			printf(
				"\tInt. Number of people immune to the disease. Default: 0\n"
			);
			printf("--simdays <simdays>\n");
			printf("\tInt. Number of days to simulate. Will terminate early if active infections hits 0. Default: 365\n");
			printf("--threads <threads>\n");
			printf("\tInt. Number of threads to use. Default: 4\n");
			printf("--daysimmune <daysimmune>\n");
			printf(
				"\tInt. Number of days immunity lasts. Default: Equals <simdays>\n"
			);
			printf("--ventilators <ventilators>\n");
			printf(
				"\tInt. Number of ventilators available. Default: 170000"
			);
			return 0;
		}
	}
	// NOTE: updated OrginalPop once we have all the args set
	uint64_t OriginalPop = SusceptiblePop + InfectedPop + RecoveredPop;
	if(!SetDaysImmune)
	{
		DaysImmune = SimulationDays + 1;
	}
	if(DaysImmune < 0)
	{
		printf("DaysImmune is somehow less than 0\n");
		return 1;
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
		InitPerson(Person, State_Susceptible, RecoverType_Delay);
	}
	for(; PersonIndex < (SusceptiblePop + InfectedPop); PersonIndex++)
	{
		person* Person = &People[PersonIndex];
		InitPerson(Person, State_Infected, RecoverType_Probability);
	}
	for(; PersonIndex < OriginalPop; PersonIndex++)
	{
		person* Person = &People[PersonIndex];
		InitPerson(Person, State_Recovered, RecoverType_Delay);	
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

	printf("Day, Susceptible, Infected, Recovered, Dead, New Cases\n");
	LARGE_INTEGER Start = GetWallClock();
	for(uint32_t Day = 0; Day < SimulationDays; Day++)
	{
		uint64_t TotalSusceptible = 0;
		uint64_t TotalInfected = 0;
		uint64_t TotalRecovered = 0;
		uint64_t TotalDead = 0;
		uint64_t NewCases = 0;

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
				if(Person->State == State_Infected)
				{
					NewCases++;
				}
			}
			Person->DaysInState++;
		}

		uint64_t LastEndAt;
		for(int ThreadIndex = 0; ThreadIndex < MaxThreads; ThreadIndex++)
		{
			set_next_state_data* Args = &ArgsArray[ThreadIndex];
			Args->BelowCapacityDeathRate = BelowCapacityDeathRate;
			Args->AboveCapacityDeathRate = AboveCapacityDeathRate;
			Args->TotalsMutex = TotalsMutex;
			Args->EncounterRate = EncounterRate;
			Args->DaysImmune = DaysImmune;
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
			"%u, %llu, %llu, %llu, %llu, %llu\n",
			Day + 1,
			TotalSusceptible,
			TotalInfected,
			TotalRecovered,
			TotalDead,
			NewCases
		);
		if(TotalInfected == 0)
		{
			break;
		}
		// NOTE: need to track this for ventilator comparisons
		GlobalInfectedCount = TotalInfected;
	}

	LARGE_INTEGER End = GetWallClock();

	float SecondsElapsed = GetSecondsElapsed(Start, End);
	printf("Time to run %f\n", SecondsElapsed);
	fflush(stdout);
	return 0;
}