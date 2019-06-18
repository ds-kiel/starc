package org.contikios.cooja.plugins.vanet.vehicle;

import org.contikios.cooja.Mote;
import org.contikios.cooja.plugins.vanet.transport_network.junction.Lane;
import org.contikios.cooja.plugins.vanet.transport_network.junction.TiledMapHandler;
import org.contikios.cooja.plugins.vanet.vehicle.physics.DirectionalDistanceSensor;
import org.contikios.cooja.plugins.vanet.vehicle.physics.VehicleBody;
import org.contikios.cooja.plugins.vanet.world.World;
import org.contikios.cooja.plugins.vanet.world.physics.Vector2D;

import java.util.AbstractMap;
import java.util.ArrayList;
import java.util.Arrays;

public class Vehicle implements VehicleInterface {
    private Mote mote; // unique identifier
    private MessageProxy messageProxy; // used for communication with cooja motes
    private VehicleBody body; // our physics model
    private TiledMapHandler mapHandler;
    private DirectionalDistanceSensor distanceSensor;

    static final boolean OTHER_DIRECTIONS = true;
    static final boolean TILE_FREEDOM = true;


    byte[] wantedRequest = new byte[0];
    byte[] currentRequest = new byte[0];

    private int state = STATE_INIT;
    private int requestState = REQUEST_STATE_INIT;

    private Lane lane;
    private World world;

    public Vehicle(Mote mote, MessageProxy messageProxy, World world, VehicleBody body, DirectionalDistanceSensor distanceSensor) {
        this.mote = mote;
        this.messageProxy = messageProxy;
        this.body = body;
        this.distanceSensor = distanceSensor;

        this.world = world;
        this.mapHandler = world.getMapHandler();

        initRandomPos();
    }

    public World getWorld() {
        return world;
    }

    public DirectionalDistanceSensor getDistanceSensor() {
        return distanceSensor;
    }

    public Mote getMote() {
        return mote;
    }

    public VehicleBody getBody() {
        return body;
    }

    @Override
    public int getState() {
        return state;
    }


    public void step(double delta) {
        // handle messages first
        byte[] msg = null;

        msg = messageProxy.receive();
        while (msg != null) {
            handleMessage(msg);
            msg = messageProxy.receive();
        }

        state = handleStates(state);

        Vector2D wantedPos = null;

        double threshold = 0.1; // we only move if the distance to the waypoint is more than this value...

        if (state == STATE_QUEUING) {
            // and the distance to front
            double sensedDist = distanceSensor.readValue();

            // TODO: check if sensed dist is higher than the distance to startpos plus vehicle size?
            double velDist = body.getVel().length(); // we need to increase the sensor distance when moving

            velDist *= velDist; // quadratic

            threshold += velDist*2; // increase the threshold since we do not want to move over the position!

            if (sensedDist > 1.0+velDist*2 || sensedDist == -1.0) { // we start moving, if there is enough space
                wantedPos = startPos;
            }
        } else if (state == STATE_MOVING) {
            wantedPos = curWayPoint;
        }

        // now we will handle our movement
        // we are able to turn and to accelerate/decelerate
        handleVehicle(delta, wantedPos, threshold);
        handleReservation();
    }


    Vector2D startPos;
    Vector2D endPos;

    // Update the state, return value will be the next state
    private int handleStates(int state) {

        if (state == STATE_INITIALIZED) {
            return STATE_QUEUING;
        } else if (state == STATE_QUEUING) {
            if (Vector2D.distance(startPos, body.getCenter()) < 0.2) {
                messageProxy.send("J".getBytes());
                requestReservation();
                return STATE_WAITING;
            } else {
                return STATE_QUEUING;
            }
        } else if (state == STATE_WAITING) {

            if (requestState == REQUEST_STATE_ACCEPTED) {
                return STATE_MOVING;
            } else {
                return STATE_WAITING;
            }
        }
        else if (state == STATE_MOVING) {
            updateWaypoints();


            if (TILE_FREEDOM) {
                // free our reservation
                requestReservation();
            }

            if (curWayPointIndex >= waypoints.size()) {
                requestReservation();

                return STATE_LEAVING;
            }

            return STATE_MOVING;
        } else if (state == STATE_LEAVING) {
            messageProxy.send("L".getBytes());
            return STATE_FINISHED;
        } else if (state == STATE_FINISHED) {

            // we reset to some other lane
            initRandomPos();
            return STATE_INITIALIZED;
        }
        return state;
    }

    private void handleReservation() {
        if (!Arrays.equals(wantedRequest, currentRequest)) {
            // we need to update our request
            if (requestState != REQUEST_STATE_SENT) {
                // we can send our new request
                currentRequest = wantedRequest;
                messageProxy.send(currentRequest);
                requestState = REQUEST_STATE_SENT;
            }
        }
    }

    private void requestReservation() {
        TiledMapHandler.PathHelper pathHandler = mapHandler.createPathHelper();
        for(int i = curWayPointIndex; i < waypoints.size(); ++i) {
            pathHandler.reservePos(waypoints.get(i));
        }
        System.out.print("Mote " + mote.getID() + " TILES: ");
        wantedRequest = pathHandler.getByteIndices();

        if (!Arrays.equals(wantedRequest, currentRequest)) {
            // we need to update our request
            if (requestState == REQUEST_STATE_ACCEPTED) {
                requestState = REQUEST_STATE_INIT;
            }
        }
    }

    private void handleMessage(byte[] msg) {
        //System.out.println(new String(msg));

        if (state == STATE_INIT && new String(msg).equals("init")) {
            state = STATE_INITIALIZED;
        }

        // handle request states
        if (requestState == REQUEST_STATE_SENT && new String(msg).equals("ack")) {
            requestState = REQUEST_STATE_ACKNOWLEDGED;
        } else if (requestState == REQUEST_STATE_ACKNOWLEDGED && new String(msg).equals("accepted")) {
            if (Arrays.equals(wantedRequest, currentRequest)) {
                requestState = REQUEST_STATE_ACCEPTED;
            } else {
                requestState = REQUEST_STATE_INIT;
            }
        }
    }

    private ArrayList<Vector2D> waypoints;
    private Vector2D curWayPoint;
    private int curWayPointIndex = 0;

    private void updateWaypoints() {

        if (waypoints.size() == 0 || curWayPointIndex >= waypoints.size()) {
            return; // cant update if there is no way...
        }

        if (curWayPoint == null) {
            curWayPointIndex = 0;
            curWayPoint = waypoints.get(curWayPointIndex);
        }

        Vector2D pos = body.getCenter();
        // we now check the distance to our current wayPoint
        if (Vector2D.distance(curWayPoint, pos) - 0.5 < 0.001) {
            curWayPointIndex++;
            // we use the next waypoint if available...
            if (curWayPointIndex < waypoints.size()) {
                curWayPoint = waypoints.get(curWayPointIndex);
            } else {
                curWayPoint = null;
            }
        }
    }

    public ArrayList<Vector2D> getWaypoints() {
        return waypoints;
    }

    public int getCurWayPointIndex() {
        return curWayPointIndex;
    }

    private void handleVehicle(double delta, Vector2D wantedPos, double threshold) {

        double acc = 7;
        double dec = 10;
        double maxSpeed = 1.0;
        double maxTurn = (Math.PI*2)/4; //(360/4 = 90 degrees per second)

        Vector2D vel = body.getVel();
        Vector2D pos = body.getCenter();
        Vector2D dir = body.getDir();

        Vector2D acceleration = new Vector2D();


        // we check our waypoints
        if (wantedPos != null) {

            // compare the wantedDir with the current direction
            Vector2D wantedDir = Vector2D.diff(wantedPos, pos);

            double a = Vector2D.angle(dir, wantedDir);
            //System.out.println("Mote " + mote.getID() + " wants to turn " + (a/(Math.PI) * 180));

            // check our steering
            double turn = delta*maxTurn;
            double a2 = Math.abs(a);
            turn = Math.max(-a2, Math.min(a2, turn));
            dir.rotate(Math.signum(a)*turn);

            // rotate the velocity too
            vel.rotate(Math.signum(a)*turn);

            // compute angle again
            a = Vector2D.angle(wantedDir, dir);

            // check if we want to accelerate or decelerate
            if (Math.abs(a) < Math.PI/4 && Vector2D.distance(wantedPos, pos) > threshold) {

                //System.out.println("Mote " + mote.getID() + " is accelerating");
                // we start to accelerate
                double x = maxSpeed-vel.length();
                if (x > 0.1*delta) {
                    acceleration = new Vector2D(dir);
                    acceleration.scale(acc);
                }
            } else {
                //System.out.println("Mote " + mote.getID() + " is decelerating");
                // we start to decelerate
                // TODO: a bit more realistic?
                if (vel.length() > 0.1*delta) {
                    acceleration = new Vector2D(dir);
                    acceleration.scale(-dec);
                }
            }
        } else {
            // just stop
            // System.out.println("Mote " + mote.getID() + " is stopping");
            // we start to decelerate
            if (vel.length() > 0.1*delta) {
                acceleration = new Vector2D(dir);
                acceleration.scale(-dec);
            } else {
                vel.setX(0);
                vel.setY(0);
            }
        }

        // per second squared
        acceleration.scale(delta*delta);

        // accelerate!
        vel.translate(acceleration);
    }

    private void initRandomPos() {
        // init the wanted position

        AbstractMap.SimpleImmutableEntry<Lane, Vector2D> res = this.world.getFreePosition();

        this.lane = res.getKey();
        this.waypoints = this.lane.getWayPoints(this.mapHandler);

        this.body.setCenter(res.getValue()); // move to center of tile
        this.body.setDir(new Vector2D(this.lane.getDirection()));
        this.body.setVel(new Vector2D()); // reset vel

        startPos = this.lane.getEndPos();
        curWayPointIndex = 0;
        curWayPoint = null;
    }


}