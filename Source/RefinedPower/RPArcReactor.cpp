#include "RPArcReactor.h"
#include "FGFactoryConnectionComponent.h"
#include "FGPipeConnectionComponent.h"
#include "FGGameState.h"
#include "FGTimeSubsystem.h"
#include "UnrealNetwork.h"
#include "FGPowerInfoComponent.h"
#include "util/Logging.h"

ARPArcReactor::ARPArcReactor() : ARPReactorBaseActor() {
	//pwr initialized with parent's constructor

	//spotlight
	SpotLight = CreateDefaultSubobject<USpotLightComponent>(TEXT("Spotlight"));
	SpotLight->SetupAttachment(RootComponent);
	//particles
	PlasmaParticles = CreateDefaultSubobject<UParticleSystemComponent>(TEXT("PlasmaParticles"));
	PlasmaParticles->SetupAttachment(RootComponent);
	//sound
	ArcReactorSound = CreateDefaultSubobject<UAudioComponent>(TEXT("ArcReactorSound"));
	ArcReactorSound->SetupAttachment(RootComponent);

	/*############### Settup machine item inputs ###############*/
	InputConveyor1 = CreateDefaultSubobject<UFGFactoryConnectionComponent>(TEXT("InputConveyor1"));
	InputConveyor2 = CreateDefaultSubobject<UFGFactoryConnectionComponent>(TEXT("InputConveyor2"));
	InputPipe = CreateDefaultSubobject<UFGPipeConnectionComponent>(TEXT("InputPipe"));

	InputConveyor1->SetupAttachment(RootComponent);
	InputConveyor2->SetupAttachment(RootComponent);
	InputPipe->SetupAttachment(RootComponent);
	/*############################################################*/

	/*############### Settup default values for UPROPERTIES ###############*/
	SpinupRotation = FVector(0.0f,0.0f,0.0f);
	SpinupOpacity = 0.0f;
	ReactorState = EReactorState::RP_State_SpunDown;
	ReactorSpinAmount = 0;
	ReactorPrevState = EReactorState::RP_State_SpunDown;
	InputConveyor1Amount = 0;
	InputConveyor2Amount = 0;
	InputPipe1Amount = 0;
	ARPReactorBaseActor::setBaseReactorPowerProduction(5000.0f);
	MaxResourceAmount = 2000;
	MinStartAmount = 1500;
	MinStopAmount = 1000;
	PowerProducedThisCycle = 0.0f;
	PowerValuePerCycle = 30000.0f;
	particlesEnabled = false;
	bReplicates = true;

	mFactoryTickFunction.bCanEverTick = true;

	/*############################################################*/
}

void ARPArcReactor::GetLifetimeReplicatedProps(TArray< FLifetimeProperty >& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ARPArcReactor, SpinupRotation);
	DOREPLIFETIME(ARPArcReactor, SpinupOpacity);
	DOREPLIFETIME(ARPArcReactor, ReactorState);
	DOREPLIFETIME(ARPArcReactor, ReactorSpinAmount);
	DOREPLIFETIME(ARPArcReactor, ReactorPrevState);
	DOREPLIFETIME(ARPArcReactor, particlesEnabled);
	DOREPLIFETIME(ARPArcReactor, InputConveyor1Amount);
	DOREPLIFETIME(ARPArcReactor, InputConveyor2Amount);
	DOREPLIFETIME(ARPArcReactor, InputPipe1Amount);
}

void ARPArcReactor::BeginPlay() {
	//left empty
	Super::BeginPlay();
}

void ARPArcReactor::Tick(float dt) {
	//left empty
	Super::Tick(dt);

	UpdateParticleVariables();
}

/*########## Main Functions ##########*/
void ARPArcReactor::ToggleLight() {
	//toggles light on and off depending on time of day
	AFGGameState* state = (AFGGameState*)UGameplayStatics::GetGameState(this);

	if (state == nullptr) {
		return;
	}

	AFGTimeOfDaySubsystem* timeSubsystem = state->GetTimeSubsystem();

	if (timeSubsystem == nullptr) {
		return;
	}

	if (timeSubsystem->IsNight() == true) {
		SpotLight->Activate();
	}
	else {
		SpotLight->Deactivate();
	}
}

void ARPArcReactor::SetReactorState(EReactorState state) {
	ReactorPrevState = ReactorState;
	ReactorState = state;
}

void ARPArcReactor::CalcResourceState() {
	if (InputConveyor1Amount >= MinStartAmount &&
		InputConveyor2Amount >= MinStartAmount &&
		InputPipe1Amount >= MinStartAmount &&
		ReactorState == EReactorState::RP_State_SpunDown &&
		ReactorSpinAmount == 0) {

		SetReactorState(EReactorState::RP_State_SpinUp);
	}
	else if ((InputConveyor1Amount < MinStopAmount || InputConveyor2Amount < MinStopAmount || InputPipe1Amount < MinStopAmount) && ReactorState == EReactorState::RP_State_Producing) {
			
		SetReactorState(EReactorState::RP_State_SpinDown);
	}
}

void ARPArcReactor::CalcReactorState() {
	switch (ReactorState) {
		case EReactorState::RP_State_SpinUp:
			IncreaseSpinAmount();
			break;
		case EReactorState::RP_State_Producing:
			RenderStateSpunUp();
			ProduceMW();
			break;
		case EReactorState::RP_State_SpinDown:
			DecreaseSpinAmount();
			break;
		case EReactorState::RP_State_SpunDown:
			RenderStateSpunDown();
			break;
	}
	CalcAudio();
}

void ARPArcReactor::ReduceResourceAmounts() {
	if (ReactorState != EReactorState::RP_State_Producing) {
		return;
	}
	else {
		InputConveyor2Amount = FMath::Clamp(InputConveyor2Amount - 1, 0, MaxResourceAmount);
		InputConveyor1Amount = FMath::Clamp(InputConveyor1Amount - 1, 0, MaxResourceAmount);
		InputPipe1Amount = FMath::Clamp(InputPipe1Amount - 1, 0, MaxResourceAmount);
	}
}

void ARPArcReactor::UpdatePowerProducedThisCycle(float dT) {
	float tempProduced = (this->FGPowerConnection->GetPowerInfo()->GetRegulatedDynamicProduction()) * dT;
	PowerProducedThisCycle += tempProduced;
}
/*####################*/

/*########## Utility Functions ##########*/
void ARPArcReactor::IncreaseSpinAmount() {
	ReactorSpinAmount = FMath::Clamp(ReactorSpinAmount++, 0, 100);
	CalcSpinningState();
}

void ARPArcReactor::DecreaseSpinAmount() {
	ReactorSpinAmount = FMath::Clamp(ReactorSpinAmount--, 0, 100);
	CalcSpinningState();
}

void ARPArcReactor::CalcSpinningState() {
	if (ReactorSpinAmount <= 0) {
		SetReactorState(EReactorState::RP_State_SpunDown);
	}
	else if (ReactorSpinAmount >= 100) {
		SetReactorState(EReactorState::RP_State_Producing);
	}
	else {
		float temp = ReactorSpinAmount * 0.01f;
		if (particlesEnabled) {
			SpinupRotation = FVector(0, 0, temp);
		}
		else {
			SpinupRotation = FVector(0, 0, 0);
		}
		SpinupOpacity = temp;
	}
}

void ARPArcReactor::RenderStateSpunDown() {
	SpinupRotation = FVector(0);
	SpinupOpacity = 0.0f;
	mUpdateParticleVars = true;
}

void ARPArcReactor::RenderStateSpunUp() {
	if (particlesEnabled) {
		SpinupRotation = FVector(0, 0, 1);
	}
	else {
		SpinupRotation = FVector(0, 0, 0);
	}
	SpinupOpacity = 1.0f;

	mUpdateParticleVars = true;
}

void ARPArcReactor::ProduceMW() {
	ARPReactorBaseActor::startReactorPowerProduction();
}

void ARPArcReactor::UpdateParticleVariables() {
	if (mUpdateParticleVars == true) {
		mUpdateParticleVars = false;

		PlasmaParticles->SetVectorParameter(FName("OrbitRate"), SpinupRotation);
		PlasmaParticles->SetFloatParameter(FName("PlasmaOpacity"), SpinupOpacity);

		if (ReactorSpinAmount <= 50) {
			PlasmaParticles->SetVectorParameter(FName("PlasmaColour"), FVector(1.0f, 0.0f, 0.0f));
		}
		else if (ReactorSpinAmount <= 75) {
			PlasmaParticles->SetVectorParameter(FName("PlasmaColour"), FVector(1.0f, 0.473217f, 0.0f));
		}
		else {
			PlasmaParticles->SetVectorParameter(FName("PlasmaColour"), FVector(0.22684f, 1.0f, 0.0f));
		}

		
	}
}

void ARPArcReactor::CalcAudio() {
	if (!ArcReactorSound->IsPlaying()) {
		if (ReactorState == EReactorState::RP_State_SpinUp) {
			StartSpinupSound();
		}
		else if (ReactorState == EReactorState::RP_State_Producing) {
			StartProducingSound();
		}
		else if (ReactorState == EReactorState::RP_State_SpinDown) {
			StartShutdownSound();
		}
	}
}

int ARPArcReactor::getReactorSpinAmount() {
	return(ReactorSpinAmount);
}

int ARPArcReactor::getInput1Amount() {
	return(InputConveyor1Amount);
}

int ARPArcReactor::getInput2Amount() {
	return(InputConveyor2Amount);
}

int ARPArcReactor::getPipeInputAmount() {
	return(InputPipe1Amount);
}

/*#######################################*/

//tick function - primary logic
void ARPArcReactor::Factory_Tick(float dt) {
	Super::Factory_Tick(dt);

	ToggleLight();

	/*##### Collect Inputs #####*/
	/*CollectInputConveyor1*/
	ARPReactorBaseActor::collectInputResource(InputConveyor1, Conveyor1InputClass, MaxResourceAmount, InputConveyor1Amount);

	/*CollectInputConveyor2*/
	ARPReactorBaseActor::collectInputResource(InputConveyor2, Conveyor2InputClass, MaxResourceAmount, InputConveyor2Amount);

	/*CollectInputPipe1*/
	ARPReactorBaseActor::collectInputFluidResource(dt, InputPipe, Pipe1InputClass, MaxResourceAmount, InputPipe1Amount);
	/*##########################*/

	CalcResourceState();
	CalcReactorState();
	if (ReactorState == EReactorState::RP_State_Producing) {
		UpdatePowerProducedThisCycle(dt);
		if (PowerProducedThisCycle >= PowerValuePerCycle) {
			ReduceResourceAmounts();
			PowerProducedThisCycle = 0.0f;
		}
	}
	
}