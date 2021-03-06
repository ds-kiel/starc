package org.contikios.cooja.plugins.vanet.vehicle.platoon;

import org.contikios.cooja.plugins.vanet.vehicle.ChaosVehicle;
import org.contikios.cooja.plugins.vanet.vehicle.VehicleInterface;

import java.util.Observable;

public class ChaosPlatoon extends Platoon  {

    int maxSize = 1;

    protected boolean joined = false;

    protected Observable observable = new Observable();

    public ChaosPlatoon(PlatoonAwareVehicle platoonAwareVehicle, int maxSize) {
        super(platoonAwareVehicle);
        this.maxSize = maxSize;
    }

    public boolean isJoined() {
        return joined;
    }

    public void setJoined(boolean joined) {
        this.joined = joined;
    }

    public boolean isMoving() {
        return getHead().getState() > VehicleInterface.STATE_WAITING;
    }

    public boolean mayJoin(PlatoonAwareVehicle platoonAwareVehicle) {
        // we do not want to form platoons while the head is already trying to reserve its path...
        if (!isMoving() /*&& !joined*/) {
            return maxSize == -1 || this.members.size() < this.maxSize;
        } else {
            return false;
        }
    }

    public void join(PlatoonAwareVehicle platoonAwareVehicle) {
        PlatoonAwareVehicle oldTail = this.getTail();
        oldTail.setSuccessor(platoonAwareVehicle);
        platoonAwareVehicle.setPredecessor(oldTail);

        // and update the platoon of the new member!
        platoonAwareVehicle.setPlatoon(this);
        members.add(platoonAwareVehicle);
    }

    @Override
    public void leave(PlatoonAwareVehicle vehicle) {
        boolean removed = members.removeIf(
            m -> m.getID() == vehicle.getID()
        );
    }
}
