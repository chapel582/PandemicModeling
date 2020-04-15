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
Infection rate is the same regardless of the # dead 
*/

#include <time.h>
#include <stdio.h>
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
inline uint64_t Rand(uint64_t Mod)
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
inline float RandUnity()
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

int main()
{
	srand((int) time(NULL));

	LARGE_INTEGER PerformanceFrequency;
	QueryPerformanceFrequency(&PerformanceFrequency);
	GlobalPerformanceFrequency = PerformanceFrequency.QuadPart;

	// NOTE: PARAMETERS
	// NOTE: the average number of people encountered by a person each day in 
	// NOTE: a way that would tranfer the virus. 
	// NOTE: fractional means that there's a chance the person doesn't get it
	float EncounterRate = 0.25;
	// NOTE: the average time in days to recover
	int DaysToRecover = 14;
	// NOTE: how many of the infected die 
	float DeathRate = 0.01f; // TODO: update me with Korea's numbers
	// TODO: give an option for making the death rate a function of hospital capacity
	// TOOD: give an option for people becoming reinfected (maybe after a certain amount of time) 

	uint32_t SimulationDays = 100;
	// NOTE: initial conditions
	uint64_t SusceptiblePop = 999999;
	uint64_t InfectedPop = 1;
	uint64_t RecoveredPop = 0;
	uint64_t OriginalPop = SusceptiblePop + InfectedPop + RecoveredPop;

	// NOTE: Susceptible and Infected need updates
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
				Person->State = Person->NextState;
				Person->NextState = Person->State;
				Person->DaysInState = 0;	
			}
			Person->DaysInState++;
		}

		for(
			person* Person = &People[0];
			Person < &People[OriginalPop];
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
					Person->NextState = State_Infected;

					// NOTE: need to see how many people this person infected
					int EncounterRateInt = (int) EncounterRate;
					int ToInfectCount = EncounterRateInt + 1;
					person** ToInfect = (person**) malloc(
						ToInfectCount * sizeof(person*)
					);
						
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
						(float) EncounterRate - (float) EncounterRateInt
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
									RandomAlreadyPicked = true;
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
					free(ToInfect);
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
}

/*
start = time.time()


	// TODO: store these for plotting
	print(
		day + 1, TotalSusceptible, TotalInfected, TotalRecovered, TotalDead
	)

total_time = time.time() - start
print(total_time)*/