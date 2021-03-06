package org.contikios.cooja.plugins.vanet.vehicle;

import org.contikios.cooja.Mote;
import org.contikios.cooja.plugins.vanet.transport_network.TransportNetwork;
import org.contikios.cooja.plugins.vanet.vehicle.platoon.PlatoonAwareVehicle;
import org.contikios.cooja.plugins.vanet.world.World;
import org.contikios.cooja.plugins.vanet.world.physics.Vector2D;

import java.util.Collection;
import java.util.HashMap;

public class VehicleManager {
    private HashMap<Integer, VehicleInterface> vehicles = new HashMap<>();
    private World world;

    public static final int INIT_POS = -1000000;

    private int idCounter = 0;

    private int intersectionType;

    public VehicleManager(World world, int intersectionType) {
        this.world = world;
        this.intersectionType = intersectionType;
    }

    public Collection<VehicleInterface> getVehicleCollection() {
        return vehicles.values();
    }

    public VehicleInterface getVehicle(int id) {
        return this.vehicles.get(id);
    }

    public synchronized void removeVehicle(int id) {
        if (vehicles.containsKey(id)) {
            VehicleInterface v = vehicles.get(id);
            v.destroy();
            // we remove the vehicle as well
            vehicles.remove(id);
        }
    }

    public synchronized VehicleInterface createVehicle(Mote m) {
        int id = idCounter+1;
        idCounter++;

        // for each mote add a new vehicle
        VehicleInterface v = null;


        if (intersectionType == TransportNetwork.INTERSECTION_TYPE_TRAFFIC_LIGHTS) {
           v = new TrafficLightVehicle(world, m, id);
        } else {
           v = new ChaosVehicle(world, m, id);
        }

        // TODO: Inject Config here, so we can check if logs are enabled (or introduec method in the Logger)
        if (world.getTransportNetwork().getHeight() == 1 && world.getTransportNetwork().getWidth() == 1) {
            if (v instanceof PlatoonAwareVehicle) {
                v = new LogAndPlatoonAwareVehicleDecorator((PlatoonAwareVehicle) v);
            } else if (v instanceof OrderAwareVehicle) {
                v = new LogAndOrderAwareVehicleDecorator((OrderAwareVehicle) v);
            } else {
                v = new LogAwareVehicleDecorator(v);
            }
        }

        v.getBody().setCenter(
            new Vector2D(INIT_POS, INIT_POS)
        );
        vehicles.put(id, v);
        return v;
    }

    public int getTotal() {
        return idCounter;
    }
}
