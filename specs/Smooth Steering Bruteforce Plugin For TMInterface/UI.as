void BruteforceSettingsWindow() {
    UI::Dummy(vec2(0, 15));
    if(UI::CollapsingHeader("General Settings")){
        UI::Dummy(vec2(0, 2));

        UI::PushItemWidth(120);
        UI::InputTextVar("Result file name", "bf_result_filename");
        UI::PopItemWidth();

        UI::Dummy(vec2(0, 5));
        UI::Separator();
        UI::Dummy(vec2(0, 2));
        
        UI::PushItemWidth(240);
        m_n_iterations_without_update = uint(UI::SliderIntVar("Iterations without update", "don_bf_n_iterations_without_update", 1, 10000));
        UI::Dummy(vec2(0, 2));
        if(m_n_iterations_without_update == 1)
            UI::TextDimmed("The base run is updated immediately after an improvement was found.");
        else
            UI::TextDimmed("Improvements are collected for " + m_n_iterations_without_update + " iterations before the base run is updated.");
        UI::PopItemWidth();

        UI::Dummy(vec2(0, 5));
    }
    if(UI::CollapsingHeader("Evaluation")){
        UI::Dummy(vec2(0, 2));

        UI::PushItemWidth(200);
        
        if (!m_Manager.m_bfController.active) {
            m_Manager.m_bfController.m_targetID = uint(GetVariableDouble("don_bf_target_id"));
            if (UI::BeginCombo(" Optimization Target", targetNames[m_Manager.m_bfController.m_targetID])) {
                for (uint i = 0; i < targetNames.Length; i++) {
                    bool isSelected = m_Manager.m_bfController.m_targetID == i;
                    if (UI::Selectable(targetNames[i], isSelected)) {
                        m_Manager.m_bfController.m_targetID = i;
                        SetVariable("don_bf_target_id", m_Manager.m_bfController.m_targetID);
                    }
                }
                UI::EndCombo();
            }
        } else {
            UI::Text(targetNames[m_Manager.m_bfController.m_targetID]);
        }

        if (TargetType(m_Manager.m_bfController.m_targetID) == TargetType::TargetCollision){
            m_Manager.m_bfController.m_targetCollisionTypeID = uint(GetVariableDouble("don_bf_target_collision_type_id"));
            if (UI::BeginCombo(" Target Type", collisionNames[m_Manager.m_bfController.m_targetCollisionTypeID])) {
                for (uint i = 0; i < collisionNames.Length; i++) {
                    bool isSelected = m_Manager.m_bfController.m_targetCollisionTypeID == i;
                    if (UI::Selectable(collisionNames[i], isSelected)) {
                        m_Manager.m_bfController.m_targetCollisionTypeID = i;
                        SetVariable("don_bf_target_collision_type_id", m_Manager.m_bfController.m_targetCollisionTypeID);
                    }
                }
                UI::EndCombo();
            }

            // if target is checkpoint or trigger, show index
            if (TargetCollisionType(m_Manager.m_bfController.m_targetCollisionTypeID) == TargetCollisionType::Checkpoint || 
                TargetCollisionType(m_Manager.m_bfController.m_targetCollisionTypeID) == TargetCollisionType::Trigger) {
                if (!m_Manager.m_bfController.active) {
                    // target id is index+1, 0 will be used for invalid or unused in case of finish
                    
                    UI::PushItemWidth(136);
                    uint targetCollisionId = uint(Math::Max(UI::InputIntVar(" Index", "don_bf_target_collision_id", 1), 1));
                    UI::PopItemWidth();
                    // check if target id is valid
                    switch (TargetCollisionType(m_Manager.m_bfController.m_targetCollisionTypeID)) {
                        case TargetCollisionType::Checkpoint:
                            // we cant check for checkpoint count because we havent loaded a map, simple set the value, an error will be given on bruteforce start
                            SetVariable("don_bf_target_collision_id", targetCollisionId);
                            break;
                        case TargetCollisionType::Trigger:
                        {
                            uint triggerCount = GetTriggerIds().Length;
                            if (triggerCount == 0) {
                                UI::Text("No triggers found. Make sure to add triggers");
                            } else if (targetCollisionId > triggerCount) {
                                targetCollisionId = triggerCount;
                            }
                            m_Manager.m_bfController.m_targetCollisionID = targetCollisionId;
                            SetVariable("don_bf_target_collision_id", targetCollisionId);
                            break;
                        }
                    }
                } else {
                    UI::Text("" + m_Manager.m_bfController.m_targetCollisionID);
                }
            }

            UI::PopItemWidth();

            if(TargetCollisionType(m_Manager.m_bfController.m_targetCollisionTypeID) != TargetCollisionType::Finish){
                UI::Dummy(vec2(0, 5));
                UI::Separator();
                UI::Dummy(vec2(0, 2));

                UI::PushItemWidth(136);
                m_minSpeed = Math::Clamp(UI::InputFloatVar(" Min Speed (km/h)", "bf_condition_speed", 1), 0.0, 1000.0);
                SetVariable("bf_condition_speed", m_minSpeed);
                UI::PopItemWidth();
            }

            UI::Dummy(vec2(0, 5));
            UI::Separator();
            UI::Dummy(vec2(0, 2));
            
            UI::PushItemWidth(180);
            int preciseTimeExponent = UI::SliderIntVar("Precision", "don_bf_precise_time_precision", 0, 64);
            UI::PopItemWidth();

            TargetCollision::epsilon = Math::Pow(2.0, -preciseTimeExponent);
            SetVariable("don_bf_precise_time_precision", preciseTimeExponent);
        UI::Dummy(vec2(0, 2));
            UI::TextDimmed("Epsilon = 2^(-Precision) = " + DecimalFormatted(TargetCollision::epsilon/100, 16) + "s is the maximum error made when calculating the precise time.");
            UI::Dummy(vec2(0, 5));
        }
        else{
            UI::Dummy(vec2(0, 2));
            
            string text = DecimalFormatted(DistanceSpeed::bfWeight, 2) + "%%";

            UI::PushItemWidth(340);
            DistanceSpeed::bfWeight = UI::SliderFloatVar("##Distance/Speed", "bf_weight", 0, 100, text);
            UI::PopItemWidth();

            UI::Text("Distance\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\tSpeed");

            UI::Dummy(vec2(0, 7));
            UI::Separator();
            UI::Dummy(vec2(0, 2));

            UI::PushItemWidth(136);
            m_minSpeed = Math::Clamp(UI::InputFloatVar(" Min Speed (km/h)", "bf_condition_speed", 1), 0.0, 1000.0);
            UI::PopItemWidth();
            SetVariable("bf_condition_speed", m_minSpeed);
            
            if(DistanceSpeed::bfWeight != 100){
                UI::Dummy(vec2(0, 5));
                UI::Separator();
                UI::Dummy(vec2(0, 2));
                
                UI::PushItemWidth(240);
                UI::DragFloat3Var(" Point", "bf_target_point");
                UI::PopItemWidth();
                DistanceSpeed::bfPoint = Text::ParseVec3(GetVariableString("bf_target_point"));
            }
            
            if(DistanceSpeed::bfWeight == 0){
                UI::Dummy(vec2(0, 2));

                UI::PushItemWidth(136);
                DistanceSpeed::m_distanceThreshold = Math::Max(0, UI::InputFloatVar(" Distance Threshold (m)", "don_bf_distance_threshold"));
                SetVariable("don_bf_distance_threshold", DistanceSpeed::m_distanceThreshold);
            }

            UI::Dummy(vec2(0, 5));
            UI::Separator();
            UI::Dummy(vec2(0, 2));

            UI::PushItemWidth(180);
            UI::Text("Evaluation Time Range:");
            UI::Dummy(vec2(0, 2));
            UI::Text("From    ");
            UI::SameLine();
            m_evaluationMinTime = uint(UI::InputTimeVar("##evaluationmintime", "bf_eval_min_time", 10));

            UI::Text("To         ");
            UI::SameLine();
            m_evaluationMaxTime = Math::Max(uint(UI::InputTimeVar("##evaluationmaxtime", "bf_eval_max_time", 10)), m_evaluationMinTime);
            SetVariable("bf_eval_max_time", m_evaluationMaxTime);
            UI::PopItemWidth();

            UI::Dummy(vec2(0, 2));
            UI::TextDimmed("The run is evaluated within this time range.");
            UI::Dummy(vec2(0, 5));
        }
    }

    if(UI::CollapsingHeader("Input Modification")){
        UI::Dummy(vec2(0, 2));
	
		
		if(inputModificationRules.IsEmpty())
			inputModificationRules.Add(InputModificationRule());
		
		
        UI::Text("Input Modification Rules:      ");
        UI::SameLine();
		if (UI::Button("Add Rule", vec2(100,  24))) {
			inputModificationRules.Add(InputModificationRule());
		}
		
        UI::Dummy(vec2(0, 10));
		
		InputModificationRule@ currentRule = @inputModificationRules[m_currentRule];
		
		
		if (UI::BeginTable("RuleTable", 7)) {
            UI::TableSetupColumn("");
            UI::TableSetupColumn("Type");
            UI::TableSetupColumn("Probability");
            UI::TableSetupColumn("Changes");
            UI::TableSetupColumn("Time");
            UI::TableSetupColumn("Radius/Range");
            UI::TableSetupColumn("Steer Diff");
            UI::TableHeadersRow();

            // Erste Zeile
			
			for (uint i = 0; i < inputModificationRules.Length; i++) {
				InputModificationRule@ rule = @inputModificationRules[i];
				UI::TableNextRow();
				UI::TableNextColumn();
				if (UI::Button("Edit##" + i)) {
					m_currentRule = i;
				}
				UI::SameLine();
				if(inputModificationRules.Length <= 1)
					UI::BeginDisabled();
				if (UI::Button("Delete##" + i)) {
					inputModificationRules.RemoveAt(i);
					if(m_currentRule >= i && m_currentRule != 0)
						m_currentRule--;
					continue;
				}
				if(inputModificationRules.Length <= 1)
					UI::EndDisabled();
				
				if(i == m_currentRule){
					UI::TableNextColumn(); UI::Text(TMInputTypeToString(rule.inputType));
					UI::TableNextColumn(); UI::Text(rule.ProbabilityToString());
					UI::TableNextColumn(); UI::Text(rule.ChangesToString());
					UI::TableNextColumn(); UI::Text(rule.TimeToString());
					UI::TableNextColumn(); UI::Text(rule.RangeToString());
					UI::TableNextColumn(); UI::Text(rule.SteerDiffToString());
				}
				else{
					UI::TableNextColumn(); UI::TextDimmed(TMInputTypeToString(rule.inputType));
					UI::TableNextColumn(); UI::TextDimmed(rule.ProbabilityToString());
					UI::TableNextColumn(); UI::TextDimmed(rule.ChangesToString());
					UI::TableNextColumn(); UI::TextDimmed(rule.TimeToString());
					UI::TableNextColumn(); UI::TextDimmed(rule.RangeToString());
					UI::TableNextColumn(); UI::TextDimmed(rule.SteerDiffToString());
				}
			}

            // Tabelle beenden
            UI::EndTable();
		}
	
	
        UI::Dummy(vec2(0, 10));
        UI::Separator();
        UI::Dummy(vec2(0, 2));
	
	
	
        UI::PushItemWidth(150);
		if (UI::BeginCombo(" Input Type", TMInputTypeToString(currentRule.inputType))) {
			for (int i = 0; i < 4; i++) {
				bool isSelected = currentRule.inputType == i;
				if (UI::Selectable(TMInputTypeToString(TMInputType(i)), isSelected)) {
					currentRule.inputType = TMInputType(i);
				}
			}
			UI::EndCombo();
		}
		UI::PopItemWidth();



        UI::Dummy(vec2(0, 5));
        UI::Separator();
        UI::Dummy(vec2(0, 2));
	
	
	
        UI::Text("Rule Probability:");
        UI::Dummy(vec2(0, 2));
		string text = DecimalFormatted(currentRule.probability, 2) + "%%";

		UI::PushItemWidth(340);
		currentRule.probability = UI::SliderFloat("##SliderProbability", currentRule.probability, 0.0, 100.0, text);
		UI::PopItemWidth();
        
        UI::Dummy(vec2(0, 2));
        UI::TextDimmed("This rule fires with a probability of " + DecimalFormatted(currentRule.probability, 2) + "%.");



        UI::Dummy(vec2(0, 5));
        UI::Separator();
        UI::Dummy(vec2(0, 2));



        UI::Text("Number of Steering Modifications:");	
        UI::Dummy(vec2(0, 2));
        
        UI::Text("Min\t   ");
        UI::SameLine();
        UI::PushItemWidth(120);
        currentRule.minChanges = uint(Math::Max(UI::InputInt("##Steer Min Amount", currentRule.minChanges, 1), 1));
        if (currentRule.minChanges > currentRule.maxChanges) {
            currentRule.maxChanges = currentRule.minChanges;
        }

        UI::Text("Max\t  ");
        UI::SameLine();
        currentRule.maxChanges = Math::Max(UI::InputInt("##Steer Max Amount", currentRule.maxChanges, 1), 1);
        if (currentRule.maxChanges < currentRule.minChanges) {
            currentRule.minChanges = currentRule.maxChanges;
        }
        UI::PopItemWidth();

        UI::Dummy(vec2(0, 2));
		
        UI::TextDimmed("A random value between " + currentRule.minChanges + " and " + currentRule.maxChanges + " is picked and that's how many times the inputs will be modified by this rule.");

        
		
        UI::Dummy(vec2(0, 5));
        UI::Separator();
        UI::Dummy(vec2(0, 2));
	
	
	
        UI::Dummy(vec2(0, 2));
        UI::Text("Input Modifications Time Range:");
        UI::Dummy(vec2(0, 2));
        UI::Text("From       ");
        UI::SameLine();

        UI::PushItemWidth(160);
        currentRule.minTime = uint(UI::InputTime("##modifyinputsmintime", currentRule.minTime, 10));
		currentRule.maxTime = Math::Max(currentRule.minTime, currentRule.maxTime);
        
        UI::Text("To            ");
        UI::SameLine();
        currentRule.maxTime = uint(UI::InputTime("##modifyinputsmaxtime", currentRule.maxTime, 10));
		currentRule.minTime = Math::Min(currentRule.minTime, currentRule.maxTime);
        UI::PopItemWidth();
        
        UI::Dummy(vec2(0, 2));
        UI::TextDimmed("Inputs are changed at a random timestep between " + DecimalFormatted(float(currentRule.minTime)/1000, 2) + "s and " + DecimalFormatted(float(currentRule.maxTime)/1000, 2) + "s. Outside of this interval no inputs are changed by this rule.");



        UI::Dummy(vec2(0, 5));
        UI::Separator();
        UI::Dummy(vec2(0, 2));
        
		
		
        // steering modification radius
        UI::Text("Radius/Range of Input Modification:");
        UI::Dummy(vec2(0, 2));

        UI::PushItemWidth(160);
		
		UI::Text("Min          ");
        UI::SameLine();
        currentRule.minRange = UI::InputTime("##inputModificationMinRange", currentRule.minRange, 10, 0);
		currentRule.maxRange = Math::Max(currentRule.minRange, currentRule.maxRange);
		
		UI::Text("Max         ");
        UI::SameLine();
        currentRule.maxRange = UI::InputTime("##inputModificationMaxRange", currentRule.maxRange, 10, 0);
		currentRule.minRange = Math::Min(currentRule.minRange, currentRule.maxRange);
        UI::PopItemWidth();
        
        UI::Dummy(vec2(0, 2));
	
		if(currentRule.inputType == TMInputType::Steering)
			UI::TextDimmed("Steering is modified inbetween a radius of " + DecimalFormatted(float(currentRule.minRange)/1000, 2) + "s to " + DecimalFormatted(float(currentRule.maxRange)/1000, 2) + "s around the chosen timestep.");
		else
			UI::TextDimmed("The inputs are changed within are range of " + DecimalFormatted(float(currentRule.minRange)/1000, 2) + "s to " + DecimalFormatted(float(currentRule.maxRange)/1000, 2) + "s after the chosen timestep.");


		
		if(currentRule.inputType == TMInputType::Steering){
			UI::Dummy(vec2(0, 5));
			UI::Separator();
			UI::Dummy(vec2(0, 2));



			UI::Text("Steering Modification Value Range:");
			UI::Dummy(vec2(0, 2));

			UI::PushItemWidth(400);
			currentRule.minSteer = uint(UI::SliderInt(" Min Steer Diff", currentRule.minSteer, 1, 131072));
			currentRule.maxSteer = Math::Max(currentRule.minSteer, currentRule.maxSteer);
			
			currentRule.maxSteer = uint(UI::SliderInt(" Max Steer Diff", currentRule.maxSteer, 1, 131072));
			currentRule.minSteer = Math::Min(currentRule.minSteer, currentRule.maxSteer);
			UI::PopItemWidth();
			
			UI::Dummy(vec2(0, 2));
			UI::TextDimmed("As you change a range of inputs at the same time, the total amount of input changes is a lot higher than with standard bruteforce at the same 'Max Steer Diff' value. So I recommend values < 20k in most cases.");
			UI::Dummy(vec2(0, 5));
		}
    }

    if(UI::CollapsingHeader("Logging")){
        UI::Dummy(vec2(0, 2));
        
        m_log_worse_improvements = UI::CheckboxVar("Show improvements worse then the current best", "don_bf_log_worse_improvements");

        UI::Dummy(vec2(0, 2));

        m_log_input_modifications = UI::CheckboxVar("Show input modifications", "don_bf_log_input_modifications");
        UI::SameLine();
        UI::TextDimmed("after each input update.");
        
        UI::Dummy(vec2(0, 2));

        m_log_bf_parameters = UI::CheckboxVar("Print Bruteforce Parameters", "don_bf_log_bf_parameters");
        UI::SameLine();
        UI::TextDimmed("when starting bruteforce.");
        
        UI::Dummy(vec2(0, 5));
    }
	
	if(UI::CollapsingHeader("Advanced Settings")){
        UI::Dummy(vec2(0, 2));
        
        UI::BeginDisabled();
        m_finetune = UI::CheckboxVar("Finetune Improvements", "don_bf_finetune");

        if(m_finetune){
            UI::Dummy(vec2(0, 2));
            
            UI::Dummy(vec2(28, 0));
            UI::SameLine();
            UI::PushItemWidth(100);
            m_finetuneDepth = Math::Clamp(UI::InputIntVar("Finetune Depth", "don_bf_finetune_depth", 1), 1, 2);
            SetVariable("don_bf_finetune_depth", m_finetuneDepth);
            UI::PopItemWidth();
        }
        UI::Dummy(vec2(0, 2));
        
        m_adaptive_parameters = UI::CheckboxVar("Adapt Parameters", "don_bf_adaptive_parameters");
        UI::EndDisabled();

        UI::Dummy(vec2(0, 5));
    }
}