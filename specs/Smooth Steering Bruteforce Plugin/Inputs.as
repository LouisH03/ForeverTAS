class InputModificationRule{

	string ToString(){
		string str;
		
		return TMInputTypeToString(inputType )+ "\t\t" + ProbabilityToString() + "\t\t" + ChangesToString() + "\t\t" + TimeToString() + "\t\t" + RangeToString() + "\t\t" + SteerDiffToString();
	}
	
	InputModification GenerateInputModification(){
		uint modifyTime = uint(Math::Rand(minTime, maxTime) / 10) * 10;
		uint range = uint(Math::Rand(minRange, maxRange) / 10) * 10;
		int diff;
		if(inputType == TMInputType::Steering)
			diff = int(Math::Rand(minSteer, maxSteer)) * (Math::Rand(0, 1) == 0 ? -1 : 1);
		else if(inputType == TMInputType::Air)
			diff = Math::Rand(-1, 1);
		else
			diff = Math::Rand(0,1);
			
		return InputModification(inputType, modifyTime, minTime, maxTime, range, diff);
	}
	
	string ProbabilityToString(){
		return DecimalFormatted(probability, 2) + "%";
	}
	string ChangesToString(){
		return minChanges + "-" + maxChanges;
	}
	string TimeToString(){
		return DecimalFormatted(float(minTime)/1000, 2) + "-" + DecimalFormatted(float(maxTime)/1000, 2);
	}
	string RangeToString(){
		return DecimalFormatted(float(minRange)/1000, 2) + "-" + DecimalFormatted(float(maxRange)/1000, 2);
	}
	string SteerDiffToString(){
		return inputType == TMInputType::Steering? minSteer + "-" + maxSteer: "";
	}
	
	TMInputType inputType;
	float probability;
	uint minChanges = 1;
	uint maxChanges = 1;
	uint minTime;
	uint maxTime;
	uint minRange;
	uint maxRange;
	uint minSteer;
	uint maxSteer;
}

class InputModification{
	InputModification(TMInputType inputType, uint time, uint minTime, uint maxTime, uint range, uint diff) {
        this.inputType = inputType;
        this.time = time;
        this.minTime = minTime;
        this.maxTime = maxTime;
        this.range = range;
        this.diff = diff;
    }
	InputModification(){}
    ~InputModification() {}
	
	string ToString(){
		string str = TMInputTypeToString(inputType) + "\t" + DecimalFormatted(float(time)/1000, 2) + "\t"  + DecimalFormatted(float(range)/1000, 2) + "\t";
		
		switch (inputType) {
			case TMInputType::Steering:
			{
				str += diff;
				break;
			}
			case TMInputType::Gas:
			case TMInputType::Brake:
			{
				str += diff == 1? "Press": "Release";
				break;
			}
			case TMInputType::Air:
			{
				if(diff == -1)
					str += "Left";
				else if(diff == 0)
					str += "Zero";
				else
					str += "Right";
				break;
			}
		}
		return str;
	}
	
	TMInputType inputType;
	uint time;
	uint minTime;
	uint maxTime;
	uint range;
	int diff;
}

namespace Inputs{
    void FillMissingInputs(SimulationManager@ simManager) {
        // fill in a steering/acceleration/brake value for next tick if it is empty, using the previous tick's value
        TM::InputEventBuffer@ inputBuffer = simManager.InputEvents;
        // to check a input type
        EventIndices actionIndices = inputBuffer.EventIndices;

        // steering
		{
			auto originalSteeringValuesIndices = simManager.InputEvents.Find(-1, InputType::Steer);
			array<uint> originalSteeringValues = array<uint>();
			for (uint i = 0; i < originalSteeringValuesIndices.Length; i++) {
				originalSteeringValues.Add(inputBuffer[originalSteeringValuesIndices[i]].Value.Analog);
			}
			auto originalSteeringTimes = array<uint>();
			for (uint i = 0; i < originalSteeringValuesIndices.Length; i++) {
				originalSteeringTimes.Add((inputBuffer[originalSteeringValuesIndices[i]].Time - 100010) / 10);
			} 

			int minTime = 0;
			int maxTime = (int(m_simulationHorizon) - 10) / 10;

			// if no steering occurred at the start, add steering value of 0 to start
			if (originalSteeringTimes.Length == 0 || originalSteeringTimes.Length > 0 && originalSteeringTimes[0] != 0) {
				originalSteeringTimes.InsertAt(0, 0);
				originalSteeringValues.InsertAt(0, 0);
				// also manually add the first steering value to the input buffer
				inputBuffer.Add(0, InputType::Steer, 0);
			}

			int currentOriginalSteeringTimesIndex = originalSteeringTimes.Length - 1;

			// iterate through all the times and fill in the empty steering values with the previous steering value
			for (int i = maxTime; i >= minTime; i--) {
				if (uint(i) > originalSteeringTimes[currentOriginalSteeringTimesIndex]) {
					inputBuffer.Add(i * 10, InputType::Steer, originalSteeringValues[currentOriginalSteeringTimesIndex]);
				} else {
					currentOriginalSteeringTimesIndex--;
					if (currentOriginalSteeringTimesIndex < 0) {
						break;
					}
				}
			}
		}
		
		

        // acceleration
        {
            auto originalAccelerationValuesIndices = simManager.InputEvents.Find(-1, InputType::Up);
            array<uint> originalAccelerationValues = array<uint>();
            for (uint i = 0; i < originalAccelerationValuesIndices.Length; i++) {
                originalAccelerationValues.Add(inputBuffer[originalAccelerationValuesIndices[i]].Value.Binary == false ? 0 : 1);
            }
            auto originalAccelerationTimes = array<uint>();
            for (uint i = 0; i < originalAccelerationValuesIndices.Length; i++) {
                originalAccelerationTimes.Add((inputBuffer[originalAccelerationValuesIndices[i]].Time - 100010) / 10);
            }

            int minTime = 0;
            int maxTime = (int(m_simulationHorizon) - 10) / 10;
            
            // if no acceleration occurred at the start, add acceleration value of 0 to start
            if (originalAccelerationTimes.Length == 0 || originalAccelerationTimes.Length > 0 && originalAccelerationTimes[0] != 0) {
                originalAccelerationTimes.InsertAt(0, 0);
                originalAccelerationValues.InsertAt(0, 0);
                // also manually add the first acceleration value to the input buffer
                inputBuffer.Add(0, InputType::Up, 0);
            }

            int currentOriginalAccelerationTimesIndex = originalAccelerationTimes.Length - 1;

            // iterate through all the times and fill in the empty acceleration values with the previous acceleration value
            for (int i = maxTime; i >= minTime; i--) {
                if (uint(i) > originalAccelerationTimes[currentOriginalAccelerationTimesIndex]) {
                    inputBuffer.Add(i * 10, InputType::Up, originalAccelerationValues[currentOriginalAccelerationTimesIndex]);
                } else {
                    currentOriginalAccelerationTimesIndex--;
                    if (currentOriginalAccelerationTimesIndex < 0) {
                        break;
                    }
                }
            }
        }

		// brake
        {
            auto originalBrakeValuesIndices = simManager.InputEvents.Find(-1, InputType::Down);
            array<uint> originalBrakeValues = array<uint>();
            for (uint i = 0; i < originalBrakeValuesIndices.Length; i++) {
                originalBrakeValues.Add(inputBuffer[originalBrakeValuesIndices[i]].Value.Binary == false ? 0 : 1);
            }
            auto originalBrakeTimes = array<uint>();
            for (uint i = 0; i < originalBrakeValuesIndices.Length; i++) {
                originalBrakeTimes.Add((inputBuffer[originalBrakeValuesIndices[i]].Time - 100010) / 10);
            }

            int minTime = 0;
            int maxTime = (int(m_simulationHorizon) - 10) / 10;

            // if no brake occurred at the start, add brake value of 0 to start
            if (originalBrakeTimes.Length == 0 || originalBrakeTimes.Length > 0 && originalBrakeTimes[0] != 0) {
                originalBrakeTimes.InsertAt(0, 0);
                originalBrakeValues.InsertAt(0, 0);
                // also manually add the first brake value to the input buffer
                inputBuffer.Add(0, InputType::Down, 0);
            }

            int currentOriginalBrakeTimesIndex = originalBrakeTimes.Length - 1;

            // iterate through all the times and fill in the empty brake values with the previous brake value
            for (int i = maxTime; i >= minTime; i--) {
                if (uint(i) > originalBrakeTimes[currentOriginalBrakeTimesIndex]) {
                    inputBuffer.Add(i * 10, InputType::Down, originalBrakeValues[currentOriginalBrakeTimesIndex]);
                } else {
                    currentOriginalBrakeTimesIndex--;
                    if (currentOriginalBrakeTimesIndex < 0) {
                        break;
                    }
                }
            }
        }
    }

    void CopyInputs(array<TM::InputEvent>@ bufferA, TM::InputEventBuffer@ bufferB){
        bufferA.Clear();
        for (uint i = 0; i < bufferB.Length; i++) {
            bufferA.Add(bufferB[i]);
        }
    }

    void CopyInputs(TM::InputEventBuffer@ bufferA, array<TM::InputEvent>@ bufferB){
        bufferA.Clear();
        for (uint i = 0; i < bufferB.Length; i++) {
            bufferA.Add(bufferB[i]);
        }
    }

    void GenerateInputModifications(){
        BruteforceController@ bfController = m_Manager.m_bfController;
        RunInfo@ currentRun = m_Manager.m_bfController.currentRun;
        RunInfo@ finetuneBaseRun = m_Manager.m_bfController.finetuneBaseRun;
		
        if(!bfController.isFinetuning){
			for(uint i = 0; i < inputModificationRules.Length; i++){
				InputModificationRule@ rule = @inputModificationRules[i];
				if(Math::Rand(0.0, 1.0) < rule.probability/100){
					uint n_changes = Math::Rand(rule.minChanges, rule.maxChanges);
					for(uint j = 0; j < n_changes; j++){
						currentRun.inputModifications.Add(rule.GenerateInputModification());
					}
				}
			}
			
			if(currentRun.inputModifications.IsEmpty()){ // no rule fired 
				InputModificationRule@ rule = @inputModificationRules[Math::Rand(0, inputModificationRules.Length-1)];
				uint n_changes = Math::Rand(rule.minChanges, rule.maxChanges);
				for(uint j = 0; j < n_changes; j++){
					currentRun.inputModifications.Add(rule.GenerateInputModification());
				}
			}
        }
        else{
			/*
			// TODO: Finetuning 
            currentRun.n_steeringModifications = finetuneBaseRun.n_steeringModifications;

            for(uint j = 0; j < finetuneBaseRun.n_steeringModifications; j++){
                currentRun.steerDiffs.Add(finetuneBaseRun.steerDiffs[j]);
                currentRun.steerRadii.Add(finetuneBaseRun.steerRadii[j]);
                currentRun.steerTicks.Add(finetuneBaseRun.steerTicks[j]);
            }
            FinetuneNeighbour(bfController.finetuneCounter);

            if(m_finetuneDepth == 2 && bfController.finetuneCounter2 != bfController.finetuneCounter){
                FinetuneNeighbour(bfController.finetuneCounter2);
            }

            do {
                if(m_finetuneDepth == 1){
                    bfController.finetuneCounter++;
                }
                else{
                    bfController.finetuneCounter2++;

                    if(float(bfController.finetuneCounter2)/2/finetuneBaseRun.n_steeringModifications == 3){
                        bfController.finetuneCounter2 = 0;
                        bfController.finetuneCounter++;
                    }
                }
                
                if(bfController.finetuneCounter < bfController.finetuneCounter2 && !(bfController.finetuneCounter/6 == bfController.finetuneCounter2/6 && (bfController.finetuneCounter%6)/2 == (bfController.finetuneCounter2%6)/2))
                    break;
                if(bfController.finetuneCounter == bfController.finetuneCounter2)
                    break;
                if(float(bfController.finetuneCounter)/2/finetuneBaseRun.n_steeringModifications == 3)
                    break;
            }
            while(true);
			*/
        }
    }
	
	/* TODO: Finetuning
    void FinetuneNeighbour(int counter){
        RunInfo@ currentRun = m_Manager.m_bfController.currentRun;
        uint modifySteeringMinTime = Math::Max(0, m_modifySteeringMinTime);
        uint modifySteeringMaxTime = m_timeLimit-10;
        modifySteeringMaxTime = m_modifySteeringMaxTime == 0 ? modifySteeringMaxTime : Math::Min(modifySteeringMaxTime, m_modifySteeringMaxTime);

        uint i = (counter/6);
        int diff = (counter % 2) == 0? -1: 1;

        if((counter%6)/2 == 0){
            currentRun.steerDiffs[i] = Math::Clamp(currentRun.steerDiffs[i] + diff, -131072, 131072);
        }
        else if((counter%6)/2 == 1){
            currentRun.steerRadii[i] = Math::Max(0, currentRun.steerRadii[i] + diff);
        }
        else {
            currentRun.steerTicks[i] = Math::Clamp(currentRun.steerTicks[i] + diff*10, modifySteeringMinTime, modifySteeringMaxTime);
        }
    }
	*/

    void RandomNeighbour() {
        RunInfo@ currentRun = m_Manager.m_bfController.currentRun;
        GenerateInputModifications();
        TM::InputEventBuffer@ inputBuffer = m_Manager.m_simManager.InputEvents;

        uint rewindIndex = Math::UINT_MAX;
        uint lowestTimeModified = Math::UINT_MAX;


        //array<int> steeringModifications((modifySteeringMaxTime - modifySteeringMinTime)/10+1);
		
		
		for(uint i = 0; i < currentRun.inputModifications.Length; i++){
			InputModification@ modification = @currentRun.inputModifications[i];
			int modifyTime = int(modification.time);
			int range = int(modification.range);
			int diff = modification.diff;
			

			int minTime = int(modification.minTime);
			int maxTime = int(modification.maxTime == 0 ? m_timeLimit-10 : Math::Min(modification.maxTime, m_timeLimit-10));
			
			switch (modification.inputType) {
				case TMInputType::Steering:
					for(int j = - range; j <= range; j+=10){ // range == radius
						int currentTime = modifyTime+j;
						if(currentTime < minTime || currentTime > maxTime) 
							continue; 
							
						auto modifyIndex = inputBuffer.Find(currentTime, InputType::Steer);
						
						int oldSteerValue = inputBuffer[modifyIndex[0]].Value.Analog;

						int steeringDiff = int(Math::Round(diff * Math::Pow(Math::Cos((Math::PI * j) / (2 * (range+1))), 2)));

						int newValue = Math::Clamp(oldSteerValue + steeringDiff, -65536, 65536);
						inputBuffer[modifyIndex[0]].Value.Analog = newValue;

						lowestTimeModified = Math::Min(lowestTimeModified, currentTime);

						//steeringModifications[(currentTime - m_modifySteeringMinTime)/10] += steeringDiff;
					}
					break;
				case TMInputType::Gas:
				{
					lowestTimeModified = Math::Min(lowestTimeModified, modifyTime);
					for(int currentTime = modifyTime; currentTime <= Math::Min(modifyTime + range, maxTime); currentTime += 10){
						auto modifyIndex = inputBuffer.Find(currentTime, InputType::Up);
						
						inputBuffer[modifyIndex[0]].Value.Binary = diff == 1;
					}
					break;
				}
				case TMInputType::Brake:
				{
					lowestTimeModified = Math::Min(lowestTimeModified, modifyTime);
					for(int currentTime = modifyTime; currentTime <= Math::Min(modifyTime + range, maxTime); currentTime += 10){
						auto modifyIndex = inputBuffer.Find(currentTime, InputType::Down);
						
						inputBuffer[modifyIndex[0]].Value.Binary = diff == 1;
					}
					break;
				}
				case TMInputType::Air:
				{
					lowestTimeModified = Math::Min(lowestTimeModified, modifyTime);
					for(int currentTime = modifyTime; currentTime <= Math::Min(modifyTime + range, maxTime); currentTime += 10){
						auto modifyIndex = inputBuffer.Find(currentTime, InputType::Steer);
						
						inputBuffer[modifyIndex[0]].Value.Analog = diff * 65536;
					}
					break;
				}
			}
		}

		/*
        uint steeringSum = 0;
        uint weightedSteeringSum = 0;

        for(uint i = 0; i < steeringModifications.Length; i++){
            weightedSteeringSum += Math::Abs(steeringModifications[i])*i;
            steeringSum += Math::Abs(steeringModifications[i]);
        }
        currentRun.avgSteeringModificationTick = (m_modifySteeringMinTime + float(weightedSteeringSum)/steeringSum*10)/1000;
        currentRun.totalSteerChanges = steeringSum;
		*/
		


        if (lowestTimeModified == 0 || lowestTimeModified == Math::UINT_MAX) {
            rewindIndex = 0;
        } else {
            rewindIndex = lowestTimeModified / 10 - 1;
        }

        currentRun.rewindIndex = rewindIndex;
        CopyInputs(currentRun.inputs, inputBuffer);
    }
}
