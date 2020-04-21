#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <windows.h>

int64_t GlobalPerformanceFrequency;
uint64_t GlobalInfectedCount;
uint64_t GlobalVentilatorCount; 
// NOTE: an option for removing an individual and their first-degree 
// NOTE: infected contacts once they show symptoms
bool GlobalSymptomaticRemoval = FALSE;
// NOTE: number of days they are in the incubation period
uint32_t GlobalIncubationDays = 5;
float GlobalSymptomaticChance = 0.12f;

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

// NOTE: need randoms between 0.0 and 1.0 
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

struct person;
typedef struct person
{
	state State;
	state NextState;
	recover_type RecoverType;
	uint8_t DaysInState; 
	uint8_t ContactIndex;
	uint8_t Symptomatic;
	uint8_t Contacted;
	struct person** Contacts;
} person;

void InitPerson(
	person* Person,
	state State,
	recover_type RecoverType,
	int ReproductiveRate
)
{
	Person->State = State;
	Person->NextState = State;
	Person->RecoverType = RecoverType;
	Person->DaysInState = 0;
	Person->Symptomatic = 0;
	Person->Contacted = 0; 
	size_t ContactArraySize = 4 * ReproductiveRate * sizeof(person*);
	Person->Contacts = (person**) malloc(ContactArraySize);
	memset(Person->Contacts, 0, ContactArraySize);
	Person->ContactIndex = 0;
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
			// CONT: probabilistic recovery instead of delay recovery
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

			if(Person->Contacted > 0 && GlobalSymptomaticRemoval)
			{
				ShouldRecover = TRUE;
			}
			// NOTE: If the person is symptomatic and we're simulating removing 
			// CONT: symptomatic individuals, we should remove them here
			else if(
				(Person->Symptomatic) && 
				(GlobalSymptomaticRemoval) &&
				(Person->DaysInState > GlobalIncubationDays)
			)
			{
				ShouldRecover = TRUE;
				for(
					int ContactIndex = 0;
					ContactIndex < Person->ContactIndex;
					ContactIndex++
				)
				{
					person* PersonToContact = Person->Contacts[ContactIndex];
					PersonToContact->Contacted = TRUE;
				}
			}

			if(ShouldRecover)
			{
				float Value = RandUnity();
				float DeathRate;
				// NOTE: the 0.05 * GlobalInfectedCount is based on New York's
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

				if(RandUnity() < EncounterRateFractional)
				{
					person* RandomPerson;
					bool RandomAlreadyPicked = FALSE;
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
						PersonToInfect->Symptomatic = (
							RandUnity() < GlobalSymptomaticChance
						);
						Person->Contacts[Person->ContactIndex] = PersonToInfect;
						Person->ContactIndex++;
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
	// CONT: a way that would tranfer the virus. 
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
	// CONT: 213779 cases and 11586 deaths
	float AboveCapacityDeathRate = 0.055f;
	// NOTE: ventilator estimate based on # in the US https://www.washingtonpost.com/health/2020/03/13/coronavirus-numbers-we-really-should-be-worried-about/
	GlobalVentilatorCount = 170000;
	// NOTE: The percentage of people infected who need a ventilator

	// NOTE: initial conditions
	uint64_t SusceptiblePop = 999999;
	uint64_t InfectedPop = 1;
	uint64_t RecoveredPop = 0;
	uint64_t DeadPop = 0;

	// NOTE: Other arguments
	uint32_t SimulationDays = 365;
	// NOTE: The # of days you keep your immunity
	// NOTE: By default, it's "infinite", i.e. SimulationDays + 1
	int DaysImmune = -1;
	int MaxThreads = 4;
	bool EndEarly = TRUE;

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
				"%llu", Argv, ArgumentIndex, "--dead", &DeadPop
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
		else if(strcmp(Argv[ArgumentIndex], "--dontendearly") == 0)
		{
			EndEarly = FALSE;
		}
		else if(strcmp(Argv[ArgumentIndex], "--removesymptomatic") == 0)
		{
			GlobalSymptomaticRemoval = TRUE;
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
			printf("--dead <dead>\n");
			printf(
				"\tInt. Number of people dead due to the disease. Default: 0\n"
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
			printf("--removesymptomatic\n");
			printf(
				"\tWhether or not to remove symptomatic individuals and their first order contacts. Default: FALSE"
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
	float ReproductiveRate = EncounterRate / (1.0f / ((float) DaysSick));
	int ReproductiveRateInt = (int) ReproductiveRate;
	if(ReproductiveRateInt == 0)
	{
		ReproductiveRateInt += 1;
	}

	// NOTE: currently People mem block is never freed 
	// CONT: this is an OK assumption since it's basically used until the death 
	// CONT: of the program
	person* People = (person*) malloc(OriginalPop * sizeof(person));
	uint64_t PersonIndex;
	for(PersonIndex = 0; PersonIndex < SusceptiblePop; PersonIndex++)
	{
		person* Person = &People[PersonIndex];
		InitPerson(
			Person, State_Susceptible, RecoverType_Delay, ReproductiveRateInt
		);
	}
	for(; PersonIndex < (SusceptiblePop + InfectedPop); PersonIndex++)
	{
		person* Person = &People[PersonIndex];
		InitPerson(
			Person, State_Infected, RecoverType_Probability, ReproductiveRateInt
		);
	}
	for(; PersonIndex < OriginalPop; PersonIndex++)
	{
		person* Person = &People[PersonIndex];
		InitPerson(
			Person, State_Recovered, RecoverType_Delay, ReproductiveRateInt
		);
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
	uint32_t Day;
	uint64_t TotalSusceptible = 0;
	uint64_t TotalInfected = 0;
	uint64_t TotalRecovered = 0;
	uint64_t TotalDead = 0;
	uint64_t NewCases = 0;
	for(Day = 0; Day < SimulationDays; Day++)
	{
		TotalSusceptible = 0;
		TotalInfected = 0;
		TotalRecovered = 0;
		TotalDead = 0;
		NewCases = 0;

		for(
			PersonIndex = 0;
			PersonIndex < OriginalPop;
			PersonIndex++
		)
		{
			person* Person = &People[PersonIndex]; 
			if(Person->State != Person->NextState)
			{
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

		TotalDead += DeadPop;
		printf(
			"%u, %llu, %llu, %llu, %llu, %llu\n",
			Day + 1,
			TotalSusceptible,
			TotalInfected,
			TotalRecovered,
			TotalDead,
			NewCases
		);
		if(TotalInfected == 0 && EndEarly)
		{
			break;
		}
		// NOTE: need to track this for ventilator comparisons
		GlobalInfectedCount = TotalInfected;
	}

	LARGE_INTEGER End = GetWallClock();

	float SecondsElapsed = GetSecondsElapsed(Start, End);
	printf("Days to 0 infected: %u\n", Day + 1);
	printf("Total recovered: %lld\n", TotalRecovered); 
	printf(
		"%% of total population recovered: %f\n", 
		100.0f * ((float) TotalRecovered) / ((float) OriginalPop)
	); 
	printf("Total Dead: %lld\n", TotalDead); 
	printf(
		"%% of total population dead: %f\n",
		100.0f * ((float) TotalDead) / ((float) OriginalPop)
	);
	printf(
		"%% of infected dead: %f\n", 
		100.0f * ((float) TotalDead) / ((float) (TotalRecovered + TotalDead))	
	);
	printf("Time to run %f\n", SecondsElapsed);
	fflush(stdout);
	return 0;
}