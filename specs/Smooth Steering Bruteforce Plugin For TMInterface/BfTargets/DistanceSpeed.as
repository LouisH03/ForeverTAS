namespace DistanceSpeed {
    float bfWeight;
    float m_distanceThreshold;
    vec3 bfPoint;

    void HandleInitialPhase(SimulationManager@ simManager, BFEvaluationResponse&out response, const BFEvaluationInfo&in info) {
        int tickTime = simManager.TickTime;
        
        m_Manager.m_bfController.currentRun.checkpointCount = simManager.PlayerInfo.CurCheckpointCount;
        
        if(int(m_evaluationMinTime) <= tickTime && tickTime <= int(m_evaluationMaxTime)){
            auto state = simManager.Dyna.CurrentState;
            auto loc = state.Location;
            auto pos = loc.Position;
            
            float dist = calculateDistance3D(pos);
            float speed = state.LinearSpeed.Length() * 3.6;
            double value = ((1000-speed)*bfWeight + (100-bfWeight)*dist)/100;
            if(IsBetter(value, tickTime, m_Manager.m_bfController.currentRun) && speed >= m_minSpeed){ 
                m_Manager.m_bfController.currentRun.value = value;
                m_Manager.m_bfController.currentRun.tick = tickTime;
                m_Manager.m_bfController.currentRun.description = createDescription(tickTime, dist, speed);
            }
        }

        if(tickTime == int(m_evaluationMaxTime)){
            m_Manager.m_bfController.currentRun.isEvaluated = true;
            if(m_Manager.m_bfController.currentRun.value == 0){
                print("\nBase run did not reach speed minimum (" + m_minSpeed + "km/h). Starting Search for a Base run...", Severity::Warning);
            }
            else{
                if(bfWeight == 0 && m_Manager.m_bfController.currentRun.value < m_distanceThreshold){
                    m_timeLimit = tickTime;
                }
            }
            print("\tTime (s)\tDistance (m)\tSpeed (km/h)\tValue");
            print("------------------------------------------------------------------");
            print("[Base]\t" + m_Manager.m_bfController.currentRun.description, Severity::Success);
            print("------------------------------------------------------------------\n");
        }
        return;
    }

    void HandleSearchPhase(SimulationManager@ simManager, BFEvaluationResponse&out response, const BFEvaluationInfo&in info) {
        int tickTime = simManager.TickTime;

        m_Manager.m_bfController.currentRun.checkpointCount = simManager.PlayerInfo.CurCheckpointCount;


        if(int(m_evaluationMinTime) <= tickTime && tickTime <= int(m_evaluationMaxTime) && (!m_bfKeepCPs || m_Manager.m_bfController.currentRun.checkpointCount == m_Manager.m_bfController.baseRun.checkpointCount)){
            auto state = simManager.Dyna.CurrentState;
            auto loc = state.Location;
            auto pos = loc.Position;
            float dist = calculateDistance3D(pos);
            float speed = state.LinearSpeed.Length() * 3.6;
            double value = ((1000-speed)*bfWeight + (100-bfWeight)*dist)/100;
            if(IsBetter(value, tickTime, m_Manager.m_bfController.currentRun) && speed >= m_minSpeed){ 
                m_Manager.m_bfController.currentRun.value = value;
                m_Manager.m_bfController.currentRun.tick = tickTime;
                m_Manager.m_bfController.currentRun.description = createDescription(tickTime, dist, speed);
            }
        }

        if(tickTime == int(m_evaluationMaxTime)){
            m_Manager.m_bfController.currentRun.isEvaluated = true;
        }
        return;
    }

    float calculateDistance3D(vec3 pos) {
        float x2 = bfPoint[0];
        float y2 = bfPoint[1];
        float z2 = bfPoint[2];
        return Math::Sqrt(Math::Pow(pos.x - x2, 2) + Math::Pow(pos.y - y2, 2) + Math::Pow(pos.z - z2, 2));
    }

    string createDescription(int tickTime, float dist, float speed){
        string description;
        description += DecimalFormatted(float(tickTime)/1000, 2) + "\t\t";
        description += DecimalFormatted(dist, 3) + "\t\t";
        description += DecimalFormatted(speed, 3) + "\t\t";
        description += DecimalFormatted(m_Manager.m_bfController.currentRun.value, 6);
        return description;
    }

    bool IsBetter(RunInfo@ runA, RunInfo@ runB){
        return IsBetter(runA.value, runA.tick, runB);
    }

    bool IsBetter(double valueA, uint tickA, RunInfo@ runB){
        if(valueA == 0)
            return false;

        if(valueA != 0 && runB.value == 0)
            return true;

        if(bfWeight == 0)
            return (valueA <= m_distanceThreshold && tickA < runB.tick) || (tickA == runB.tick && valueA < runB.value) || (runB.value > m_distanceThreshold && valueA < runB.value);
        else 
            return valueA < runB.value;
    }

    void PrintMessage(RunInfo@ baseRun, RunInfo@ currentRun, RunInfo@ bestRun){
        if (@baseRun == null) {
            print("[Search phase] Found new base run: " +  DecimalFormatted(currentRun.value, 16) + " m", Severity::Success);
        } else {
            bool _isBetter = IsBetter(currentRun, bestRun);
            string message = "";
            message += "[" + (m_log_worse_improvements? m_Manager.m_bfController.improvementCounter: m_Manager.m_bfController.bestRunImprovementCounter) + "]\t";
            // message += " Found run: " + currentRun.description;
            message += currentRun.description;
            if(_isBetter || m_log_worse_improvements){
                print(message, _isBetter? Severity::Success: Severity::Info);
            }
        }
    }

    void OnSearchPhaseBegin(){
        print("\tTime (s)\tDistance (m)\tSpeed (km/h)\tValue");
        print("------------------------------------------------------------------");
    }
}