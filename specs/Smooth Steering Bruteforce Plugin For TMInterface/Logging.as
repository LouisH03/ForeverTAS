namespace Logging{

    void PrintParameterSettings(){
        string message = "Parameters: ";
        message += "\n   Iterations without update: " + m_n_iterations_without_update;
        message += "\n   Result File: " + GetVariableString("bf_result_filename");
        message += "\n   Bruteforce Target: ";
        if(TargetType(m_Manager.m_bfController.m_targetID) == TargetType::TargetCollision){
            message += "Target Collision ";
            switch(TargetCollisionType(m_Manager.m_bfController.m_targetCollisionTypeID)){
                case TargetCollisionType::Finish:
                    message += "(Finish)";
                    break;
                case TargetCollisionType::Trigger:
                    message += "(Trigger)";
                    break;
                case TargetCollisionType::Checkpoint:
                    message += "(Checkpoint)";
            }
            message +="\n      Max Precise Time Error: " + DecimalFormatted(TargetCollision::epsilon, 16);
        }
        else{
            message += "Distance/Speed";
            if(DistanceSpeed::bfWeight == 0)
                message += "\n   Distance Threshold: " + DecimalFormatted(DistanceSpeed::m_distanceThreshold, 2);
        }
        if(m_minSpeed > 0 && !(TargetType(m_Manager.m_bfController.m_targetID) == TargetType::TargetCollision) && TargetCollisionType(m_Manager.m_bfController.m_targetCollisionTypeID) == TargetCollisionType::Finish)
            message += "\n      Min Speed: " + m_minSpeed;
        message += "\n   Finetune parameters: " + m_finetune;
        if(m_finetune)
            message += "\n      Finetune Depth: " + m_finetuneDepth;
		message += "\n   Rules:";
		for(uint i = 0; i < inputModificationRules.Length; i++)
			message += "\n      " + inputModificationRules[i].ToString();
        print(message);
    }
    void OnSimulationBegin(){
        print("\n\n------------------------------------ Starting Bruteforce --------------------------------------");
        if(m_log_bf_parameters)
            PrintParameterSettings();
    }

    void OnInitialPhaseBegin(){
        print("\n#################### Initial Phase  | iteration: " + m_iterations + " ####################");
    }

    void OnSearchPhaseBegin(TargetType targetType){
        print("\n#################### Search Phase   | iteration: " + m_iterations + " ####################");
        if(targetType == TargetType::TargetCollision)
            TargetCollision::OnSearchPhaseBegin();
        else 
            DistanceSpeed::OnSearchPhaseBegin();
    }

    void OnFinetunePhaseBegin(TargetType targetType){
        print("\n#################### Finetune Phase | iteration: " + m_iterations + " ####################");
        
        if(targetType == TargetType::TargetCollision)
            TargetCollision::OnSearchPhaseBegin();
        else 
            DistanceSpeed::OnSearchPhaseBegin();
    }

    void OnSimulationEnd(){
        print("[AS] Bruteforce finished");
    }
    
    void PrintInputBuffer() {
        // somehow this doesnt show steering events properly after i filled in the missing inputs, but it does work for acceleration and brake
        print(m_Manager.m_bfController.m_simManager.InputEvents.ToCommandsText(InputFormatFlags(3)));
    }

    void PrintRunInfo(RunInfo@ run){
        print("\nInput Modifications:");
        print("\tType\t\tTime\tRange\tInput");
        for(uint i = 0; i < run.inputModifications.Length; i++){
            print(i + ".\t" + run.inputModifications[i].ToString());
        }
        // print("Average Steering Modification Time: " + run.avgSteeringModificationTick);
        // print("Total Input Changes: " + run.totalSteerChanges + "\n");
    }

	/* TODO: Finetuning
    void PrintFinetuneInfo(RunInfo@ finetuneBaseRun, RunInfo@ bestRun){
        print("\nInput Finetunings:");
        print("\tTime\t\tRadius\t\tDiff");
        for(uint j = 0; j < finetuneBaseRun.n_steeringModifications; j++){
            string message = j + ".";
            message += "\t" + DecimalFormatted(float(bestRun.steerTicks[j])/1000, 2) + " " + FormatDiff(float(int(bestRun.steerTicks[j]) - finetuneBaseRun.steerTicks[j])/1000, 2);
            message += "\t" + DecimalFormatted(float(bestRun.steerRadii[j])/100, 2) + " " + FormatDiff(float(int(bestRun.steerRadii[j]) - finetuneBaseRun.steerRadii[j])/100, 2);
            message += "\t" + bestRun.steerDiffs[j] + " " + FormatDiff(bestRun.steerDiffs[j] - finetuneBaseRun.steerDiffs[j]);
            print(message);
        }
		
        print("Average Steering Modification Time: " + DecimalFormatted(bestRun.avgSteeringModificationTick, 4) + " " + FormatDiff(bestRun.avgSteeringModificationTick - finetuneBaseRun.avgSteeringModificationTick, 4));
        print("Total Input Changes: " + bestRun.totalSteerChanges + " " + FormatDiff(int(bestRun.totalSteerChanges) - finetuneBaseRun.totalSteerChanges) + "\n");
		
    }
	*/

    void SaveSolutionToFile(SimulationManager@ simManager, RunInfo@ run) {
        // m_commandList.Content = simManager.InputEvents.ToCommandsText();
        // only save if the time we found is the best time ever, currently also saves when an equal time was found and accepted
        string resultFileName = GetVariableString("bf_result_filename");
        CommandList commandList;
        commandList.Content = "# Found precise time: " + DecimalFormatted(run.value, 16) + ", iterations: " + m_iterations + "\n";
		commandList.Content += simManager.InputEvents.ToCommandsText(InputFormatFlags(3));
		commandList.Save(resultFileName);
    }

    void PrintBruteforceInfo(RunInfo@ bestRun, int iterations) { // TODO
        string message = "[AS] ";

        message += bestRun.description;

        message += " | ";
        message += "iterations: " + Text::FormatInt(m_iterations); // + " | iters/sec: " + DecimalFormatted(m_iterationsPerSecond, 2);

        //print(message);
        if(m_log_input_modifications)
            PrintRunInfo(bestRun);
		
		// float currentTime = float(Time::Now);
		// currentTime /= 1000.0f;
		// float timeSinceLastUpdate = currentTime - m_lastIterationsPerSecondUpdate;
		// m_iterationsPerSecond = float(m_iterationsCounter) / timeSinceLastUpdate;
		// m_lastIterationsPerSecondUpdate = currentTime;
		// m_iterationsCounter = 0;
    }

    void PrintMessage(TargetType targetType, RunInfo@ baseRun, RunInfo@ currentRun, RunInfo@ bestRun){
        if(targetType == TargetType::DistanceSpeed)
            DistanceSpeed::PrintMessage(baseRun, currentRun, bestRun);
        else
            TargetCollision::PrintMessage(baseRun, currentRun, bestRun);
    }
}
