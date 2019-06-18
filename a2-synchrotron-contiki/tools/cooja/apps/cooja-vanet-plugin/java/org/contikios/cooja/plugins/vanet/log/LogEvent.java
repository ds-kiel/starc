package org.contikios.cooja.plugins.vanet.log;

public class LogEvent {

    private String name;
    private long simulationTime;
    private String data;
    private Integer modeID;

    public LogEvent(String name, long simulationTime, String data, Integer modeID) {
        this.name = name;
        this.simulationTime = simulationTime;
        this.data = data;
        this.modeID = modeID;
    }

    public LogEvent(String name, long simulationTime, String data) {
        this(name, simulationTime, data, null);
    }

    public LogEvent(String name, long simulationTime) {
        this(name, simulationTime, null, null);
    }

    public String getName() {
        return name;
    }

    public long getSimulationTime() {
        return simulationTime;
    }

    public String getData() {
        return data;
    }

    public Integer getModeID() {
        return modeID;
    }
}
