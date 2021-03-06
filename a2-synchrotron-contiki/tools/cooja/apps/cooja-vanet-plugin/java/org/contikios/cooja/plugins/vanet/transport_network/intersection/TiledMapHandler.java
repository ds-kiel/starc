package org.contikios.cooja.plugins.vanet.transport_network.intersection;

import org.contikios.cooja.plugins.Vanet;
import org.contikios.cooja.plugins.vanet.world.physics.Vector2D;

import java.util.HashSet;

public class TiledMapHandler {

    private int width = 0;
    private int height = 0;

    private Vector2D offset;

    public TiledMapHandler(int width, int height, Vector2D offset) {
        this.width = width;
        this.height = height;
        this.offset = offset;
    }

    public double getTileScaling() {
        return Vanet.SCALE;
    }

    public int getNumTiles() {
        return width*height;
    }

    public int getWidth() {
        return width;
    }

    public int getHeight() {
        return height;
    }

    public Vector2D getOffset() {
        return offset;
    }

    public PathHelper createPathHelper() {
        return new PathHelper(this);
    }

    public class PathHelper {

        private TiledMapHandler mapHandler;

        private HashSet<Integer> reserved = new HashSet<>();

        public PathHelper(TiledMapHandler mapHandler) {
            this.mapHandler = mapHandler;
        }

        public void reservePos(Vector2D pos) {
            int index = mapHandler.posToIndex(pos);
            reserved.add(index);
        }

        public byte[] getByteIndices() {

            int[] temp = reserved.stream().mapToInt(Integer::intValue).toArray();
            byte[] bytes = new byte[temp.length+1];

            // ugly byte arr copying...
            bytes[0] = 'R';
            for(int i = 0; i < temp.length; ++i) {
                //System.out.print((temp[i]&0xFF) + ", ");
                bytes[i+1] = (byte) (temp[i]&0xFF);
            }
            //System.out.print("\n");
            return bytes;
        }
    }

    public int posToIndex(Vector2D pos) {
        int x = Math.max(0, Math.min((int) ((pos.getX()-offset.getX()) / getTileScaling()), width-1));
        int y = Math.max(0, Math.min((int) ((pos.getY()-offset.getY()) / getTileScaling()), height-1));
        return y*width+x;
    }

    public Vector2D indexToPos(int i) {
        int x = i%width;
        int y = (i-x)/width;

        return new Vector2D((x+0.5)*getTileScaling()+offset.getX(), (y+0.5)*getTileScaling()+offset.getY());
    }
}
