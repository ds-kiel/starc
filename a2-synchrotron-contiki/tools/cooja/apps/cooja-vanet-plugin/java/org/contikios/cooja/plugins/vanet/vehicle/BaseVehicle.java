package org.contikios.cooja.plugins.vanet.vehicle;

import org.contikios.cooja.Mote;
import org.contikios.cooja.plugins.Vanet;
import org.contikios.cooja.plugins.vanet.transport_network.intersection.Intersection;
import org.contikios.cooja.plugins.vanet.transport_network.intersection.Lane;
import org.contikios.cooja.plugins.vanet.transport_network.intersection.TiledMapHandler;
import org.contikios.cooja.plugins.vanet.vehicle.physics.DirectionalDistanceSensor;
import org.contikios.cooja.plugins.vanet.vehicle.physics.VehicleBody;
import org.contikios.cooja.plugins.vanet.world.World;
import org.contikios.cooja.plugins.vanet.world.physics.Physics;
import org.contikios.cooja.plugins.vanet.world.physics.Vector2D;

import java.util.AbstractMap;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collection;

abstract class BaseVehicle implements VehicleInterface {
    protected VehicleBody body; // our physics model
    protected DirectionalDistanceSensor distanceSensor;

    protected int id;

    protected int state = STATE_INIT;

    Vector2D startPos;

    protected World world;

    protected Intersection currentIntersection;
    protected Lane currentLane;
    protected Lane targetLane;


    final double ACCELERATION = 4*0.5; // m/s*s
    final double DECELERATION = 8*0.5; // m/s*s
    final double MAX_SPEED = 13.8889; // m/s, 50 km/h
    final double MAX_TURN = (Math.PI*2.0)/4.0; //(360/4 = 90 degrees per second)


    public BaseVehicle(World world, Mote m, int id) {

        this.world = world;
        this.id = id;

        body = new VehicleBody(String.valueOf(id));

        body.setCenter(
                new Vector2D(
                        m.getInterfaces().getPosition().getXCoordinate(),
                        m.getInterfaces().getPosition().getYCoordinate()
                )
        );

        distanceSensor = new DirectionalDistanceSensor(body);
    }


    void init() {
        initRandomPos();

        world.getPhysics().addBody(body);
        world.getPhysics().addSensor(distanceSensor);
    }

    public void destroy() {
        // remove stuff
        world.getPhysics().removeBody(body);
        world.getPhysics().removeSensor(distanceSensor);
    }

    public World getWorld() {
        return world;
    }

    public DirectionalDistanceSensor getDistanceSensor() {
        return distanceSensor;
    }

    public VehicleBody getBody() {
        return body;
    }

    @Override
    public Intersection getCurrentIntersection() {
        return currentIntersection;
    }

    @Override
    public int getState() {
        return state;
    }

    public int getID() {
        return id;
    }

    public void step(double delta) {
        state = handleStates(state);
        Vector2D wantedPos = null;

        if (state == STATE_QUEUING || state == STATE_WAITING) {
            if (Vector2D.distance(startPos, body.getCenter()) > Vanet.SCALE*0.1) {
                wantedPos = startPos;
            }
        } else if (state == STATE_MOVING || state == STATE_LEAVING || state == STATE_LEFT) {
            updateWaypoints();
            wantedPos = getNextWaypoint();
        }

        double maxBrakeDist = wantedPos != null ? Vector2D.distance(wantedPos, body.getCenter()) : 0;

        if (distanceSensor.readValue() >= 0) {
            if (state != STATE_MOVING || curWayPointIndex >= waypoints.size()-Lane.STEPS_INTO_LANE) {
                maxBrakeDist = Math.max(0, Math.min(distanceSensor.readValue() - 2.5*body.getRadius(), maxBrakeDist));
            }
        }
        // now we will handle our movement
        // we are able to turn and to accelerate/decelerate
        drive(delta, wantedPos, maxBrakeDist);
    }

    public Vector2D getNextWaypoint() {
        Vector2D originalWP = null;
        Vector2D nextWP = null;

        if (curWayPointIndex < waypoints.size()) {
            originalWP = waypoints.get(curWayPointIndex);
            nextWP = originalWP;

            Vector2D originDir = Vector2D.diff(body.getCenter(), originalWP);

            if (originDir.length() > 0) {
                double threshold = 0.1*Vanet.SCALE;
                originDir.normalize();

                int i = curWayPointIndex+1;

                // minus 1 since we do not want the endpoint to be our direct target
                int max = waypoints.size();
                // TODO: With the predecessor logic, we dont really need to do this anymore?
                if (this.targetLane == null || !this.targetLane.isFinalEndLane()) {
                    max -= Lane.STEPS_INTO_LANE;
                }
                while(i < max) {
                    Vector2D possWP = waypoints.get(i);
                    double dist = Vector2D.distance(Physics.closestPointOnLine(body.getCenter(), originDir, possWP), possWP);
                    if (dist < threshold) {
                        nextWP = possWP;
                        ++i;
                    } else {
                        break;
                    }
                }
            }
        }
        return nextWP;
    }



    // Update the state, return value will be the next state
    protected abstract int handleStates(int state);

    protected ArrayList<Vector2D> waypoints = new ArrayList<>();
    protected int curWayPointIndex = 0;

    public ArrayList<Vector2D> getWaypoints() {
        return waypoints;
    }

    public int getCurWayPointIndex() {
        return curWayPointIndex;
    }

    protected void updateWaypoints() {
        if (curWayPointIndex >= waypoints.size()) {
            return;
        }

        Vector2D pos = body.getCenter();
        Vector2D curWayPoint = waypoints.get(curWayPointIndex);
        double dist = Vector2D.distance(curWayPoint, pos);

        if (dist < 0.5 * Vanet.SCALE) {
            curWayPointIndex++;
        }
    }

    protected void handleVehicle(double delta, Vector2D wantedDir, double wantedVel) {
        Vector2D vel = body.getVel();
        Vector2D dir = body.getDir();
        Vector2D acceleration = new Vector2D();

        if (wantedDir != null && wantedDir.length() > 0) {
            // check if we need to rotate
            double a = Vector2D.angle(dir, wantedDir);
            // check our steering
            double turn = Math.signum(a)*Math.min(Math.abs(a), delta*MAX_TURN);
            dir.rotate(turn);
            // rotate the velocity too
            vel.rotate(turn);
            dir.normalize();
        }

        double x = wantedVel-vel.length();

        if (x > ACCELERATION*delta) {
            // we accelerate
            acceleration = new Vector2D(dir);
            acceleration.scale(ACCELERATION);
        } else if (x <= 0.0) {
            // we decelerate
            if (vel.length() > DECELERATION*delta) {
                acceleration = new Vector2D(dir);
                acceleration.scale(-DECELERATION);
            } else {
                vel.setX(0);
                vel.setY(0);
            }
        }

        // per second squared
        acceleration.scale(delta);

        // accelerate!
        vel.translate(acceleration);
    }

    protected double calculateBreakDist(double vel) {
        return vel*vel/(2*DECELERATION);
    }

    protected double calculateMaxVel(double breakDist) {
        return Math.sqrt(breakDist*2*DECELERATION);
    }

    protected void drive(double delta, Vector2D wantedPos, double maxBrakeDistance) {

        Vector2D wantedDir = null;
        double wantedVel = 0;

        Vector2D pos = body.getCenter();
        Vector2D dir = body.getDir();

        // we check our waypoints
        if (wantedPos != null) {

            // compare the wantedDir with the current direction
            wantedDir = Vector2D.diff(wantedPos, pos);
            double a = Vector2D.angle(dir, wantedDir);

            wantedVel = MAX_SPEED;
            // we now check if we could brake in the given distance
            if (maxBrakeDistance >= 0.0) {
                double maxVel = calculateMaxVel(maxBrakeDistance);
                if (wantedVel > maxVel) {
                    wantedVel = maxVel;
                }
            }

            // we slow down our wantedVelocity based on the angle
            double turnSlowDown = Math.pow(Math.abs(a) / MAX_TURN, 1.0/3.0);
            wantedVel *= (1.0 - Math.min(turnSlowDown, 1.0));
        }

        handleVehicle(delta, wantedDir, wantedVel);
    }

    protected void initRandomPos() {
        // init the wanted position

        AbstractMap.SimpleImmutableEntry<Lane, Vector2D> res = this.world.getFreePosition();
        Lane lane = res.getKey();

        this.body.setCenter(res.getValue()); // move to center of tile
        this.body.setDir(new Vector2D(lane.getDirectionVector()));
        this.body.setVel(new Vector2D()); // reset vel

        initLane(lane);
    }

    protected void initLane(Lane lane) {
        Collection<Lane> possibleLanes = lane.getEndIntersection().getPossibleLanes(lane);
        // use random lane for now
        // TODO: Use some planned path through multiple intersections
        targetLane = possibleLanes.stream().skip((int) (possibleLanes.size() * World.getRand().nextFloat())).findAny().get();

        this.waypoints = lane.getWayPoints(targetLane);
        this.currentIntersection = lane.getEndIntersection();

        currentLane = lane;
        startPos = lane.getEndPos();
        curWayPointIndex = 0;
    }

    @Override
    public void setMote(Mote mote) {
        //NOOP we are not saving the mote atm
    }

    @Override
    public int getTurn() {
        return currentLane.computeTurn(targetLane);
    }
}