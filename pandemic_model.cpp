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
	float EncounterRate;
	int DaysInState; // NOTE: only tracked for IR states
} person;

void InitPerson(person* Person, state State, float EncounterRate)
{
	Person->State = State;
	Person->NextState = State;
	Person->EncounterRate = EncounterRate;
	Person->DaysInState = 0; // NOTE: only tracked for IR states
}

typedef struct set_next_state_data
{
	float DeathRate;
	HANDLE TotalsMutex;
	int MaxEncounterRate;
	int DaysToRecover;
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
	int MaxEncounterRate = Args->MaxEncounterRate;
	int DaysToRecover = Args->DaysToRecover;
	person* People = Args->People;
	uint64_t StartAt = Args->StartAt;
	uint64_t EndAt = Args->EndAt;
	uint64_t OriginalPop = Args->OriginalPop;
	uint64_t TotalSusceptible = 0;
	uint64_t TotalInfected = 0;
	uint64_t TotalRecovered = 0;
	uint64_t TotalDead = 0;
	srand(((int) time(NULL)) + GetCurrentThreadId());
	
	// NOTE: allocate once per thread for speed
	person** ToInfect = (person**) malloc(
		MaxEncounterRate * sizeof(person*)
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

			if(Person->DaysInState > DaysToRecover)
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
				// NOTE: need to see how many people this person infected
				int EncounterRateInt = (int) Person->EncounterRate;
					
				int LivingFound = 0;
				while(LivingFound < EncounterRateInt)
				{
					// TODO: consider if having the living in a linked list could help lookups like this go faster

					// TODO: factor out finding a non-dead, unrecovered person
					uint64_t RandomIndex = Rand(OriginalPop);
					person* RandomPerson = &People[RandomIndex];

					// NOTE: Make sure we found a living person
					if(RandomPerson->State != State_Dead)
					{
						ToInfect[LivingFound++] = RandomPerson;
					}
				}

				float EncounterRateFractional = (
					(float) Person->EncounterRate - (float) EncounterRateInt
				);
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

int main()
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
	int MaxEncounterRate = 10;
	// NOTE: the average time in days to recover
	int DaysToRecover = 14;
	// NOTE: how many of the infected die 
	float DeathRate = 0.01f; // TODO: update me with Korea's numbers
	// TODO: give an option for making the death rate a function of hospital capacity
	// TOOD: give an option for people becoming reinfected (maybe after a certain amount of time) 

	// NOTE: initial conditions
	uint64_t SusceptiblePop = 999999;
	uint64_t InfectedPop = 1;
	uint64_t RecoveredPop = 0;
	uint64_t OriginalPop = SusceptiblePop + InfectedPop + RecoveredPop;

	// NOTE: Other arguments
	uint32_t SimulationDays = 100;
	int MaxThreads = 4;

	// NOTE: Susceptible and Infected need updates
	// NOTE: currently People mem block is never freed 
	// NOTE: this is an OK assumption since it's basically used until the death 
	// NOTE: of the program
	person* People = (person*) malloc(OriginalPop * sizeof(person));
	uint64_t PersonIndex;
	for(PersonIndex = 0; PersonIndex < SusceptiblePop; PersonIndex++)
	{
		person* Person = &People[PersonIndex];
		InitPerson(Person, State_Susceptible, EncounterRate);
	}
	for(; PersonIndex < (SusceptiblePop + InfectedPop); PersonIndex++)
	{
		person* Person = &People[PersonIndex];
		InitPerson(Person, State_Infected, EncounterRate);
	}
	for(; PersonIndex < OriginalPop; PersonIndex++)
	{
		person* Person = &People[PersonIndex];
		InitPerson(Person, State_Recovered, EncounterRate);	
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
			person* Person = &People[0];
			Person < &People[OriginalPop];
			Person++
		)
		{
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
			Args->MaxEncounterRate = MaxEncounterRate;
			Args->DaysToRecover = DaysToRecover;
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

	return 0;
}