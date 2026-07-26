package android.hardware;

import java.util.Collections;
import java.util.List;

public final class MuplarSensorManager extends SensorManager {
    public MuplarSensorManager() {
        super();
    }

    @Override
    public Sensor getDefaultSensor(int type) {
        return null;
    }

    @Override
    public Sensor getDefaultSensor(int type, boolean wakeUp) {
        return null;
    }

    @Override
    public List<Sensor> getSensorList(int type) {
        return Collections.emptyList();
    }

    @Override
    public List<Sensor> getDynamicSensorList(int type) {
        return Collections.emptyList();
    }

    @Override
    public boolean registerListener(SensorEventListener listener,
                                    Sensor sensor,
                                    int samplingPeriodUs) {
        return false;
    }

    @Override
    public boolean registerListener(SensorEventListener listener,
                                    Sensor sensor,
                                    int samplingPeriodUs,
                                    int maxReportLatencyUs) {
        return false;
    }

    @Override
    public void unregisterListener(SensorEventListener listener) {
    }

    @Override
    public void unregisterListener(SensorEventListener listener, Sensor sensor) {
    }
}
