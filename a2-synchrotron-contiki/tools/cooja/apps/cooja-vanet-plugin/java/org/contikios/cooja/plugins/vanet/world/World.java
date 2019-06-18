package org.contikios.cooja.plugins.vanet.world;

import org.contikios.cooja.Mote;
import org.contikios.cooja.interfaces.Position;
import org.contikios.cooja.plugins.vanet.log.Logger;
import org.contikios.cooja.plugins.vanet.transport_network.TransportNetwork;
import org.contikios.cooja.plugins.vanet.transport_network.junction.Lane;
import org.contikios.cooja.plugins.vanet.transport_network.junction.TiledMapHandler;
import org.contikios.cooja.plugins.vanet.vehicle.LogAwareVehicleDecorator;
import org.contikios.cooja.plugins.vanet.vehicle.Vehicle;
import org.contikios.cooja.plugins.vanet.vehicle.VehicleInterface;
import org.contikios.cooja.plugins.vanet.world.physics.Computation.Intersection;
import org.contikios.cooja.plugins.vanet.world.physics.Physics;
import org.contikios.cooja.plugins.vanet.world.physics.Sensor;
import org.contikios.cooja.plugins.vanet.world.physics.Vector2D;

import java.util.*;

public class World {

    private Physics physics;

    private HashMap<Integer, VehicleInterface> vehicles = new HashMap<>();
    private Collection<Sensor> sensors = new ArrayList<>();

    private TransportNetwork transportNetwork;

    private TiledMapHandler mapHandler;

    private long currentMS = 0;

    public static Random RAND;

    public World() {
        this.physics = new Physics();
        this.transportNetwork = new TransportNetwork(1,1);
    }

    public TiledMapHandler getMapHandler() {
        return this.transportNetwork.getJunction().getMapHandler();
    }

    public long getCurrentMS() {
        return currentMS;
    }

    public void simulate(long deltaMS) {

        currentMS += deltaMS;
        double delta = deltaMS / 1000.0;

        // read the position back from the simulation

        // step through every vehicle and update the position in the simulation
        Collection<VehicleInterface> vehicleCollection = vehicles.values();
        vehicleCollection.forEach(this::readPosition);

        // simulate components for each step
        this.physics.simulate(delta);

        // update other components here like sensors etc...
        sensors.forEach( s -> s.update(this.physics, delta));

        // step through every vehicle logic and execute it
        vehicleCollection.forEach(v -> v.step(delta));

        // step through every vehicle and update the position in the simulation
        vehicleCollection.forEach(this::writeBackPosition);
    }

    private void writeBackPosition(VehicleInterface v) {
        Mote mote = v.getMote();
        Position pos = mote.getInterfaces().getPosition();
        Vector2D center = v.getBody().getCenter();
        pos.setCoordinates(center.getX(), center.getY(), 0);
    }

    private void readPosition(VehicleInterface v) {
        Mote mote = v.getMote();
        Vector2D center = v.getBody().getCenter();
        Position pos = mote.getInterfaces().getPosition();
        center.setX(pos.getXCoordinate());
        center.setY(pos.getYCoordinate());
    }

    public VehicleInterface getVehicle(int ID) {
        return this.vehicles.get(ID);
    }

    public VehicleInterface getVehicle(Mote m) {
        return this.vehicles.get(m.getID());
    }

    public void addVehicle(VehicleInterface v) {

        this.vehicles.put(v.getMote().getID(), v);
        this.sensors.add(v.getDistanceSensor());
        // we also add the vehicle body to the physics
        this.physics.addBody(v.getBody());
        writeBackPosition(v); // support initial position setting
    }



    public AbstractMap.SimpleImmutableEntry<Lane, Vector2D> getFreePosition() {
       Lane l = this.transportNetwork.getRandomStartLane();

       Vector2D d = new Vector2D(l.getDirection());
       d.scale(-1);

       // we translate the endpos a bit such that we check right from the beginning
       Vector2D endPos = new Vector2D(l.getDirection());
       endPos.scale(0.5);
       endPos.add(l.getEndPos());


       // we need to check the collision
        Collection<Intersection> intersections = this.physics.computeLineIntersections(endPos,d);

        double maxDist = intersections.stream().
                            map(i -> i.distance).
                            filter(x -> x >= 0.0).
                            max(Double::compareTo).
                            orElse(0.0);

        Vector2D freePos = new Vector2D(d);
        freePos.scale(maxDist+0.5+1.0);
        freePos.add(endPos);
        return new AbstractMap.SimpleImmutableEntry<>(l, freePos);
    }
}
