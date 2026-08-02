// constants
const array<string> targetNames = { "Target Collision", "Distance/Speed" };
const array<string> collisionNames = { "Finish", "Checkpoint", "Trigger" };

array<InputModificationRule> inputModificationRules;
uint m_currentRule;

Manager @m_Manager;

// bruteforce vars
int m_timeLimit;

// settings vars

bool m_bfKeepCPs;
float m_minSpeed;

uint m_evaluationMinTime;
uint m_simulationHorizon = 6000;

uint m_n_iterations_without_update;

bool m_log_worse_improvements;
bool m_log_input_modifications;
bool m_log_bf_parameters;

bool m_adaptive_parameters;
bool m_finetune;
uint m_finetuneDepth;

// info vars
uint m_iterations = 0; // total iterations
uint m_iterationsCounter = 0; // iterations counter, used to update the iterations per second
float m_iterationsPerSecond = 0.0f; // iterations per second
float m_lastIterationsPerSecondUpdate = 0.0f; // last time the iterations per second were updated


/* enum definitions, because somehow we cant define enums inside a class */
enum TargetType {
    TargetCollision,
    DistanceSpeed
}

enum TargetCollisionType {
    Finish,
    Checkpoint,
    Trigger
}

enum TMInputType{
	Steering,
	Gas,
	Brake,
	Air
}

string TMInputTypeToString(TMInputType inputType) {
    switch (inputType) {
        case TMInputType::Steering: 
			return PadRight("Steering", 10);
        case TMInputType::Gas: 
			return PadRight("Gas", 10);
        case TMInputType::Brake: 
			return PadRight("Brake", 10);
        case TMInputType::Air: 
			return PadRight("Air", 10);
    }
    return "Unknown";
}

class RunInfo{ 
    RunInfo(uint iteration) {
        this.iteration = iteration;
    }
    ~RunInfo() {}
	double value;
    bool isEvaluated;
	// float avgSteeringModificationTick;
    // uint totalSteerChanges;
    uint iteration;
    uint rewindIndex;
    array<TM::InputEvent> inputs;
    string description;
	array<InputModification> inputModifications;
    uint tick;
    uint checkpointCount;
}; 



// helper functions
string DecimalFormatted(float number, int precision = 10) {
    return Text::FormatFloat(number, "{0:10f}", 0, precision);
}
string DecimalFormatted(double number, int precision = 10) {
    return Text::FormatFloat(number, "{0:10f}", 0, precision);
}

string FormatDiff(int number){
    if(number == 0)
        return "\t";
    return "(" + (number >= 0? "+": "-") + Math::Abs(number) + ")";
}
string FormatDiff(float number, int precision = 10){
    if(number == 0)
        return "\t";
    return "(" + (number >= 0? "+": "-") + DecimalFormatted(Math::Abs(number), precision) + ")";
}
string PadRight(const string &in str, const uint targetLength, const uint8 char = ' ')
{
    const uint len = str.Length;
    if (len >= targetLength)
        return str;

    string s = str;
    s.Resize(targetLength);

    for (uint i = len; i < targetLength; i++)
        s[i] = char;

    return s;
}
 
/* SIMULATION MANAGEMENT */

class BruteforceController {
    BruteforceController() {}
    ~BruteforceController() {}

    // reset variables bruteforce needs
    void SetBruteforceVariables(SimulationManager@ simManager) {
        // General Variables
        m_timeLimit = int(m_simulationHorizon);

        m_iterations = 0;
        m_iterationsCounter = 0;
        m_iterationsPerSecond = 0.0f;
        m_lastIterationsPerSecondUpdate = 0.0f;

        @baseRun = null;
        @currentRun = null;
        @bestRun = null;
        @finetuneBaseRun = null;
        @finetuneStartRun = null;

        // PreciseTime Variables
        TargetCollision::isEstimating = false;
        TargetCollision::delta = 1.0;
        TargetCollision::epsilon = 0.01;
        TargetCollision::currentCoeff = 1.0;
		improvementCounter = 0;
        bestRunImprovementCounter = 0;
        isFinetuning = false;
        finetuneCounter = 0;
        finetuneCounter2 = 0;
        finetuneStepCounter = 1;
        
        m_targetID = uint(GetVariableDouble("don_bf_target_id"));
        m_targetCollisionID = uint(Math::Max(GetVariableDouble("don_bf_target_collision_id"), 1.0));
        m_targetCollisionTypeID = uint(GetVariableDouble("don_bf_target_collision_type_id"));

        // in case of checkpoint or trigger, the index of the target

        // check if target id is valid
        switch (TargetCollisionType(m_targetCollisionTypeID)) {
            case TargetCollisionType::Finish:
                // no need to check anything
                break;
            case TargetCollisionType::Checkpoint:
            {
                uint checkpointCount = simManager.PlayerInfo.Checkpoints.Length;
                if (m_targetCollisionID > checkpointCount) {
                    print("[AS] Checkpoint with target id " + m_targetCollisionID + " does not exist on this map, change the target id in settings to fix this issue. stopping bruteforce..", Severity::Error);
                    OnSimulationEnd(simManager);
                    return;
                }
                break;
            }
            case TargetCollisionType::Trigger:
            {
                uint triggerCount = GetTriggerIds().Length;
                if (triggerCount == 0) {
                    print("[AS] Cannot bruteforce for trigger target, no triggers were found. stopping bruteforce..", Severity::Error);
                    OnSimulationEnd(simManager);
                    return;
                }
                // if too high target id is specified, set to highest poss
                if (m_targetCollisionID > triggerCount) {
                    print("[AS] Trigger with target id " + m_targetCollisionID + " does not exist, change the target id in settings to fix this issue. stopping bruteforce..", Severity::Error);
                    OnSimulationEnd(simManager);
                    return;
                }
                break;
            }
        }
    }
    
    void StartInitialPhase() {
        m_phase = BFPhase::Initial;
        Logging::OnInitialPhaseBegin();
        StartNewIteration();
                
        Inputs::CopyInputs(currentRun.inputs, m_simManager.InputEvents);
        
        if(@bestRun != null){
            m_simManager.RewindToState(m_originalSimulationStates[bestRun.rewindIndex]);
            m_originalSimulationStates.Resize(bestRun.rewindIndex + 1);
            UpdateInputBuffer(bestRun);
        }
    }

    void StartSearchPhase() {
        m_phase = BFPhase::Search;
        Logging::OnSearchPhaseBegin(TargetType(m_targetID));
        StartSearchPhaseIteration();
    }

    void StartSearchPhaseIteration(){
        UpdateInputBuffer(baseRun);
        StartNewIteration();

        Inputs::RandomNeighbour(); 
        m_simManager.RewindToState(m_originalSimulationStates[currentRun.rewindIndex]);
    }

    void StartNewIteration() {
        UpdateIterationsPerSecond();
        @currentRun = RunInfo(m_iterations);
    }

    void OnSimulationBegin(SimulationManager@ simManager) {
        active = GetVariableString("controller") == "don_bf_controller";
        if (!active) {
            return;
        }

        Logging::OnSimulationBegin();

        @m_simManager = simManager;

        // knock off finish event from the input buffer
        m_simManager.InputEvents.RemoveAt(m_simManager.InputEvents.Length - 1);
        
        // handle variables 
        SetBruteforceVariables(simManager);
        UpdateSettings();

        // one time variables that cannot be changed during simulation
        Inputs::FillMissingInputs(simManager);

        m_phase = BFPhase::Initial;
        m_originalSimulationStates = array<SimulationState@>();
        m_originalSimulationStates.Clear();

        StartInitialPhase();
    }

    void OnSimulationEnd(SimulationManager@ simManager) {
        if (!active) {
            return;
        }
        Logging::OnSimulationEnd();
        active = false;

        m_originalSimulationStates.Clear();

        // set the simulation time limit to make the game quit the simulation right away, or else we'll have to wait all the way until the end of the replay..
        simManager.SetSimulationTimeLimit(0.0);
    }
	
	void UpdateInputBuffer(RunInfo@ run) {
        Inputs::CopyInputs(m_simManager.InputEvents, run.inputs);
    }

    void OnCheckpointCountChanged(SimulationManager@ simManager, int count, int target) {
        if (!active) {
            return;
        }

        if (m_simManager.PlayerInfo.RaceFinished) {
            m_simManager.PreventSimulationFinish();
        }
    }

    void CollectInitialPhaseData(SimulationManager@ simManager) {
        if (simManager.RaceTime >= 0) {
            m_originalSimulationStates.Add(m_simManager.SaveState());
        }
    }

    void HandleSearchPhase(SimulationManager@ simManager, BFEvaluationResponse&out response, BFEvaluationInfo&in info) {
		if(TargetType(m_targetID) == TargetType::DistanceSpeed)
            DistanceSpeed::HandleSearchPhase(m_simManager, response, info);
        else
            TargetCollision::HandleSearchPhase(m_simManager, response, info);

    }

    void HandleInitialPhase(SimulationManager@ simManager, BFEvaluationResponse&out response, BFEvaluationInfo&in info) {
		if(TargetType(m_targetID) == TargetType::DistanceSpeed)
            DistanceSpeed::HandleInitialPhase(m_simManager, response, info);
        else
            TargetCollision::HandleInitialPhase(m_simManager, response, info);
    }

    void OnSimulationStep(SimulationManager@ simManager) {
        if (!active) {
            return;
        }

        BFEvaluationInfo info;
        info.Phase = m_phase;
        
        BFEvaluationResponse evalResponse = OnBruteforceStep(simManager, info);

        switch(evalResponse.Decision) {
            case BFEvaluationDecision::DoNothing:
                if (m_phase == BFPhase::Initial) {
                    CollectInitialPhaseData(simManager);
                }
                break;
            case BFEvaluationDecision::Accept:
                if (m_phase == BFPhase::Initial) {
                    StartSearchPhase();
                } else{
                    if(!isFinetuning){
                        /* TODO: Adaptive Parameters
						if(m_adaptive_parameters)
                            AdaptParameters();
						*/
                        StartInitialPhase();
                    }
                    else{
                        StartSearchPhaseIteration();
                    }
                }
                break;
            case BFEvaluationDecision::Reject:
                if (m_phase == BFPhase::Initial) {
                    print("[AS] Cannot reject in initial phase, ignoring");
                } else {
                    StartSearchPhaseIteration();
                }
                break;
            case BFEvaluationDecision::Stop:
                print("[AS] Stopped");
                OnSimulationEnd(simManager);
                break;
        }
    }

    BFEvaluationResponse@ OnBruteforceStep(SimulationManager@ simManager, const BFEvaluationInfo&in info) { // TODO überarbeiten
        BFEvaluationResponse response;

        if(info.Phase == BFPhase::Initial){
            HandleInitialPhase(simManager, response, info);

            if(!currentRun.isEvaluated){
                response.Decision = BFEvaluationDecision::DoNothing;
            }
            else {
                response.Decision = BFEvaluationDecision::Accept;
                
                if(@bestRun != null){
                    if(bestRun.value != currentRun.value){
                        print(bestRun.value + " != " + currentRun.value);
                        print("Error: Base run is different to previous best run!", Severity::Error);
                        OnSimulationEnd(simManager);
                    }
                    else {
                        @baseRun = @bestRun;
                        baseRun.iteration = m_iterations;
                    }
                }
                else{
                    @baseRun = @currentRun;
                    @bestRun = @currentRun;
                }
            }
            return response;
        }
        else if(info.Phase == BFPhase::Search){
            HandleSearchPhase(simManager, response, info);

            if(!currentRun.isEvaluated){
                response.Decision = BFEvaluationDecision::DoNothing;
                return response;
            }
            
            if (IsBetter(currentRun, baseRun)) {
                improvementCounter++;

                Logging::PrintMessage(TargetType(m_targetID), baseRun, currentRun, bestRun);

                if (IsBetter(currentRun, bestRun)) { // check if best time ever was driven
                    @bestRun = @currentRun;
                    Logging::SaveSolutionToFile(simManager, bestRun);
                    bestRunImprovementCounter++;
                }
            }

            if((m_iterations - baseRun.iteration > m_n_iterations_without_update && improvementCounter != 0) && !isFinetuning ||
                isFinetuning && float(finetuneCounter)/2/finetuneBaseRun.inputModifications.Length == 3)
            {
                if(isFinetuning){
                    if(bestRunImprovementCounter == 0){
                        if(finetuneStepCounter == 1){
                            print("No improvement found in Finetune Phase...");
                            print("------------------------------------------------------------------");
                        }
                        else{
							/* TODO: Finetuning
                            if(m_log_input_modifications)
                                Logging::PrintFinetuneInfo(finetuneStartRun, bestRun);
							*/
                        }
                        isFinetuning = false;
                        finetuneStepCounter = 1;
                    }
                    else{
                        print("--  " + finetuneStepCounter + "\t----------------------------------------------------------");
                        @finetuneBaseRun = @bestRun;
                        finetuneStepCounter++;
                    }
                }
                else{
                    print("------------------------------------------------------------------");

                    if(m_log_input_modifications)
                        Logging::PrintRunInfo(bestRun);

                    if(!isFinetuning && m_finetune){
                        isFinetuning = true;
                        @finetuneBaseRun = @bestRun;
                        @finetuneStartRun = @bestRun;
                        Logging::OnFinetunePhaseBegin(TargetType(m_targetID));
                    }
                }
            
                improvementCounter = 0;
                bestRunImprovementCounter = 0;
                finetuneCounter = 0;
                finetuneCounter2 = 0;

                response.Decision = BFEvaluationDecision::Accept;
            }
            else{
                response.Decision = BFEvaluationDecision::Reject;
            }
        }

        return response;
    }

    void UpdateIterationsPerSecond() {
        m_iterations++;
        m_iterationsCounter++;
    }

    bool IsBetter(RunInfo@ runA, RunInfo@ runB){
        if(TargetType(m_targetID) == TargetType::TargetCollision)
            return TargetCollision::IsBetter(runA, runB);
        else
            return DistanceSpeed::IsBetter(runA, runB);
    }

    /* void AdaptParameters(){ // TODO: AdaptiveParameters
        float maxSteerDiff = 0;
        float maxSteerRadius = 0;
        //uint minSteerDiff = 131072;
        print("");
        // uint totalSteerChanges = 0;
        for(uint i=0; i < bestRun.steerDiffs.Length; i++){
            print(i + ". Input: " + bestRun.steerDiffs[i] + "  " + bestRun.steerRadii[i]);
            uint steerMod = bestRun.steerDiffs[i] * (bestRun.steerRadii[i] + 1);
            maxSteerRadius += bestRun.steerRadii[i] * steerMod;
            maxSteerDiff += bestRun.steerDiffs[i] * steerMod;
            //minSteerDiff = Math::Min(minSteerDiff, bestRun.steerDiffs[i]);
            totalSteerChanges += steerMod;
        }
        maxSteerDiff /= float(totalSteerChanges);
        print("GDS h: " + maxSteerDiff);
        maxSteerDiff *= 2;
        maxSteerDiff = Math::Clamp(maxSteerDiff, Math::Max(float(m_modifySteeringMaxDiff)/1.4, 10), float(m_modifySteeringMaxDiff)*1.4);
        maxSteerRadius /= float(totalSteerChanges);
        print("GDS radius: " + maxSteerRadius);
        maxSteerRadius *= 2;
        maxSteerRadius = Math::Clamp(maxSteerRadius, Math::Max(float(m_steeringModificationRadius)/1.2, 5), float(m_steeringModificationRadius)*1.2);
         

        //maxSteerDiff = uint(Math::Max(Math::Min(float(maxSteerDiff) * 1.2, 131072), m_modifySteeringMaxDiff/1.2));
        //minSteerDiff = uint(Math::Max(Math::Min(float(minSteerDiff) * 1.2, maxSteerDiff), m_modifySteeringMinDiff/1.2));

        

        //maxSteerRadius = uint(Math::Max(float(m_steeringModificationRadius)/1.2, Math::Max(float(maxSteerRadius) * 1.5, 20)));

        //uint modifySteeringMaxAmount = Math::Max(uint(float(bestRun.steerDiffs.Length) * 1.5), 2);


        maxSteerDiff = uint(Math::Round(maxSteerDiff));
        maxSteerRadius = uint(Math::Round(maxSteerRadius));
        print("\nAdapting Parameters:");
        //print("minSteerDiff: " + m_modifySteeringMinDiff + " >> " + minSteerDiff);
        print("maxSteerDiff: " + m_modifySteeringMaxDiff + " >> " + maxSteerDiff);
        print("maxSteerRadius: " + m_steeringModificationRadius + " >> " + maxSteerRadius); 
        //print("modifySteerMaxAmount: " + m_modifySteeringMaxAmount + " >> " + modifySteeringMaxAmount + "\n");

        //m_modifySteeringMinDiff = minSteerDiff;
        m_modifySteeringMaxDiff = uint(maxSteerDiff);
        m_steeringModificationRadius = uint(maxSteerRadius);
        //m_modifySteeringMaxAmount = modifySteeringMaxAmount; 
    }*/

    SimulationManager@ m_simManager;
    bool active = false;
    BFPhase m_phase = BFPhase::Initial;
    bool isFinetuning = false;
    uint finetuneCounter = 0;
    uint finetuneCounter2 = 0;
    uint finetuneStepCounter = 1;

    // initialized as "finish"
    uint m_targetID = 0;
    uint m_targetCollisionID = 1;
    uint m_targetCollisionTypeID = 0;
    
    int improvementCounter = 0;
    int bestRunImprovementCounter = 0;

    array<SimulationState@> m_originalSimulationStates = {};

    RunInfo@ baseRun;
    RunInfo@ currentRun;
    RunInfo@ bestRun;
    RunInfo@ finetuneBaseRun;
    RunInfo@ finetuneStartRun;
}

class Manager {
    Manager() {
        @m_bfController = BruteforceController();
    }
    ~Manager() {}

    void OnSimulationBegin(SimulationManager@ simManager) {
        @m_simManager = simManager;
        m_simManager.RemoveStateValidation();
        m_bfController.OnSimulationBegin(simManager);
    } 

    void OnSimulationStep(SimulationManager@ simManager, bool userCancelled) {
        if (userCancelled) {
            m_bfController.OnSimulationEnd(simManager);
            return;
        }

        m_bfController.OnSimulationStep(simManager);
    }

    void OnSimulationEnd(SimulationManager@ simManager, uint result) {
        m_bfController.OnSimulationEnd(simManager);
    }

    void OnCheckpointCountChanged(SimulationManager@ simManager, int count, int target) {
        m_bfController.OnCheckpointCountChanged(simManager, count, target);
    }

    SimulationManager@ m_simManager;
    BruteforceController@ m_bfController;
}

/* these functions are called from the game, we relay them to our manager */
void OnSimulationBegin(SimulationManager@ simManager) {
    m_Manager.OnSimulationBegin(simManager);
}

void OnSimulationEnd(SimulationManager@ simManager, uint result) {
    m_Manager.OnSimulationEnd(simManager, result);
}

void OnSimulationStep(SimulationManager@ simManager, bool userCancelled) {
    m_Manager.OnSimulationStep(simManager, userCancelled);
}

void OnCheckpointCountChanged(SimulationManager@ simManager, int count, int target) {
    m_Manager.OnCheckpointCountChanged(simManager, count, target);
}


// general settings that can be updated during or outside of bruteforce and can be called at any point in time
void UpdateSettings() { 
    SimulationManager@ simManager = m_Manager.m_simManager;

    // m_target, m_targetType, m_targetId. only check this when bruteforce is inactive when updating settings, the bruteforce controller will have
    // additional checks for this by itself
    if (@simManager == null || !m_Manager.m_bfController.active) {
        // string for the target used for console settings
        m_Manager.m_bfController.m_targetID = uint(GetVariableDouble("don_bf_target_id"));
        m_Manager.m_bfController.m_targetCollisionID = uint(Math::Max(GetVariableDouble("don_bf_target_id"), 1.0));
        m_Manager.m_bfController.m_targetCollisionTypeID = uint(GetVariableDouble("don_bf_target_collision_type_id"));

        // check if target id is valid
        switch (TargetCollisionType(m_Manager.m_bfController.m_targetCollisionTypeID)) {
            case TargetCollisionType::Finish:
                // no need to check anything
                break;
            case TargetCollisionType::Checkpoint:
                // nothing can be done for checkpoints, we are not on a map
                break; 
            case TargetCollisionType::Trigger:
                // we can check for triggers because those are built into TMI
                m_Manager.m_bfController.m_targetCollisionID = Math::Min(GetTriggerIds().Length, m_Manager.m_bfController.m_targetCollisionID);
        }
        SetVariable("don_bf_target_collision_id", m_Manager.m_bfController.m_targetCollisionID);
    }

    DistanceSpeed::bfWeight = GetVariableDouble("bf_weight");
    DistanceSpeed::m_distanceThreshold = Math::Max(0.0, GetVariableDouble("don_bf_distance_threshold"));
    SetVariable("don_bf_distance_threshold", DistanceSpeed::m_distanceThreshold);
    
    m_minSpeed = GetVariableDouble("bf_condition_speed");
    DistanceSpeed::bfPoint = Text::ParseVec3(GetVariableString("bf_target_point"));
    
    m_bfKeepCPs = GetVariableBool("bf_keep_all_cps");

	// precise time precision
    int preciseTimeExponent = int(GetVariableDouble("don_bf_precise_time_precision"));
	TargetCollision::epsilon = Math::Pow(2.0, -preciseTimeExponent);

    if (@simManager != null && m_Manager.m_bfController.active) {
        m_Manager.m_simManager.SetSimulationTimeLimit(m_timeLimit + 10010); // i add 10010 because tmi subtracts 10010 and it seems to be wrong. (also dont confuse this with the other value of 100010, thats something else)
    }

    m_n_iterations_without_update = uint(GetVariableDouble("don_bf_n_iterations_without_update"));
	
	// logging
	m_log_worse_improvements = GetVariableBool("don_bf_log_worse_improvements");
    m_log_input_modifications = GetVariableBool("don_bf_log_input_modifications");
    m_log_bf_parameters = GetVariableBool("don_bf_log_bf_parameters");

    // advanced
	m_adaptive_parameters = GetVariableBool("don_bf_adaptive_parameters");

	SetVariable("don_bf_finetune", false);
	m_finetune = GetVariableBool("don_bf_finetune");
    m_finetuneDepth = uint(GetVariableDouble("don_bf_finetune_depth"));
}

void Main() {
    @m_Manager = Manager();

    RegisterVariable("don_bf_target_id", 0);
    RegisterVariable("don_bf_target_collision_id", 1);
    RegisterVariable("don_bf_target_collision_type_id", 0);
	
    RegisterVariable("don_bf_precise_time_precision", 20.0);

    RegisterVariable("don_bf_distance_threshold", 0.0);
	
	RegisterVariable("don_bf_steering_modification_radius", 0);

    RegisterVariable("don_bf_modify_steering_min_diff", 1.0);
    RegisterVariable("don_bf_modify_steering_max_diff", 10000.0);
	
    RegisterVariable("don_bf_n_iterations_without_update", 2000.0);
	
	RegisterVariable("don_bf_log_worse_improvements", true);
	RegisterVariable("don_bf_log_input_modifications", true);
    RegisterVariable("don_bf_log_bf_parameters", true);

	RegisterVariable("don_bf_adaptive_parameters", false);
	RegisterVariable("don_bf_finetune", false);
	RegisterVariable("don_bf_finetune_depth", 1);

    UpdateSettings();
	
    RegisterValidationHandler("don_bf_controller", "[AS] Don's Bruteforce Controller", BruteforceSettingsWindow);
}


PluginInfo@ GetPluginInfo()
{
    auto info = PluginInfo();
    info.Name = "Smooth Bruteforce";
    info.Author = "Don Johnson";
    info.Version = "v1.1.1";
    info.Description = "Bruteforce script for smooth steering";
    return info;
}


/*
Ideas for future updates:
   brake and release timing modification
   Time/Speed setting for Triggers and Checkpoints
   during bruteforce adapt the parameters according to the current best run
   dreieck und konstante funktion

Bugs: Evaluation time range 0.00-0.00
*/




/*
A random value between 0.00 and the given max value is generated. It becomes the radius of the generated discretized cosine wave.
For example: \n steering_modification_radius = 0.05\n max_steer_diff = 10\n The largest possible wave for these parameters would be: 1, 3, 5, 8, 9, 10, 9, 8, 5, 3, 1
These values are then simply added to the current inputs with the center of the sequence at the random time value which is generated within the given modification time range.


*/
