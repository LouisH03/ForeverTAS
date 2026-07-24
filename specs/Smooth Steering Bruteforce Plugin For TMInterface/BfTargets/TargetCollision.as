namespace TargetCollision {
    bool isEstimating = false;
    double delta;
    double epsilon;
    double currentCoeff;
    SimulationState@ originalStateBeforeTargetHit;

    void HandleInitialPhase(SimulationManager@ simManager, BFEvaluationResponse&out response, const BFEvaluationInfo&in info) {
        int tickTime = simManager.TickTime;

        bool targetReached = IsTargetReached(simManager);

        if (!isEstimating) {
            m_Manager.m_bfController.currentRun.checkpointCount = simManager.PlayerInfo.CurCheckpointCount;
            if (targetReached) {
                isEstimating = true;
            } else {
                if (tickTime > m_timeLimit) {
                    print("[Initial phase] Base run did not reach target, starting search for a base run..", Severity::Info);   
                    // m_Manager.m_bfController.currentRun.value = (simManager.RaceTime / 1000.0);
                    m_Manager.m_bfController.currentRun.isEvaluated = true;
                } else{
                    @originalStateBeforeTargetHit = simManager.SaveState();
                }
                return;
            }
        } 

        CalculatePreciseTime(simManager, targetReached);

        if(isEstimating){
            return;
        }

        print("\tTime (s)");
        print("------------------------------------------------------------------");
        if (m_Manager.m_bfController.currentRun.value != 0) {
            print("[Base]\t" + DecimalFormatted(m_Manager.m_bfController.currentRun.value, 16), Severity::Success);
        }
        else{
            print("Base run did not hit target... Starting search for base run.", Severity::Warning);
        }
        print("------------------------------------------------------------------");
            
        m_timeLimit = Math::Min(m_timeLimit, int(Math::Floor(m_Manager.m_bfController.currentRun.value * 100.0)) * 10);
        return;
    }

    void HandleSearchPhase(SimulationManager@ simManager, BFEvaluationResponse&out response, const BFEvaluationInfo&in info) {
        int tickTime = simManager.TickTime;

        bool targetReached = IsTargetReached(simManager);

        if (!isEstimating) {
            m_Manager.m_bfController.currentRun.checkpointCount = simManager.PlayerInfo.CurCheckpointCount;
            if (targetReached 
                && (!m_bfKeepCPs || m_Manager.m_bfController.currentRun.checkpointCount >= m_Manager.m_bfController.baseRun.checkpointCount)  
                && (TargetCollisionType(m_Manager.m_bfController.m_targetCollisionTypeID) == TargetCollisionType::Finish || simManager.Dyna.CurrentState.LinearSpeed.Length() * 3.6 >= m_minSpeed)) {
                isEstimating = true;
            } else {
                if (tickTime > m_timeLimit) {
                    response.Decision = BFEvaluationDecision::Reject;
                    m_Manager.m_bfController.currentRun.isEvaluated = true;
                } else{
                    @originalStateBeforeTargetHit = simManager.SaveState();
                }
                return;
            }
        }

        CalculatePreciseTime(simManager, targetReached);
    }

    bool IsTargetReached(SimulationManager@ simManager){
        switch (TargetCollisionType(m_Manager.m_bfController.m_targetCollisionTypeID)) {
            case TargetCollisionType::Finish:
                return simManager.PlayerInfo.RaceFinished;
            case TargetCollisionType::Checkpoint:
                return simManager.PlayerInfo.CurCheckpointCount == m_Manager.m_bfController.m_targetCollisionID;
            case TargetCollisionType::Trigger:
            {
                Trigger3D trigger = GetTriggerByIndex(m_Manager.m_bfController.m_targetCollisionID - 1);
                // targetReached = trigger.ContainsPoint(simManager.Dyna.CurrentState.Location.Position);
                return IsColliding(simManager, trigger);
            }
        }
        return false;
    }

    void CalculatePreciseTime(SimulationManager@ simManager, bool targetReached){
        delta /= 2;
        if (targetReached) {
            currentCoeff -= delta;
        } else {
            currentCoeff += delta;
        }

        if (delta >= epsilon) {
            simManager.RewindToState(originalStateBeforeTargetHit);
            simManager.Dyna.CurrentState.LinearSpeed *= currentCoeff;
            simManager.Dyna.CurrentState.AngularSpeed *= currentCoeff;
        }
        else{
            double preciseTime = (simManager.RaceTime / 1000.0) + (currentCoeff / 100.0) - (simManager.PlayerInfo.RaceFinished? 0.0: 0.01);
            m_Manager.m_bfController.currentRun.value = preciseTime;
            m_Manager.m_bfController.currentRun.isEvaluated = true;

            isEstimating = false;
            currentCoeff = 1.0;
            delta = 1.0;
        } 
    }

    bool IsBetter(RunInfo@ runA, RunInfo@ runB){
        return  runA.value != 0 && (runA.value < runB.value ||runB.value == 0);
    }

    void PrintMessage(RunInfo@ baseRun, RunInfo@ currentRun, RunInfo@ bestRun){
        if (@baseRun == null) {
            print("[Search phase] Found new base run with precise time: " +  DecimalFormatted(currentRun.value, 16) + " sec", Severity::Success);
        } else {
            bool _isBetter = IsBetter(currentRun, bestRun);
            string message = "[" + (m_log_worse_improvements? m_Manager.m_bfController.improvementCounter: m_Manager.m_bfController.bestRunImprovementCounter) + "]\t";
            message += DecimalFormatted(currentRun.value, 16);
            if(_isBetter || m_log_worse_improvements){
                print(message, _isBetter? Severity::Success: Severity::Info);
            }
        }
    }

    void OnSearchPhaseBegin(){
        print("\tTime (s)");
        print("------------------------------------------------------------------");
    }
}