package au.edu.jcu.v4l4j.encoder.h264;

import java.awt.image.BufferedImage;
import java.awt.image.DataBuffer;
import java.awt.image.Raster;
import java.io.Closeable;

import au.edu.jcu.v4l4j.FrameGrabber;
import au.edu.jcu.v4l4j.VideoFrame;
import au.edu.jcu.v4l4j.exceptions.UnsupportedMethod;

public class H264Picture implements Closeable, VideoFrame {
	
	static {
		try {
			System.loadLibrary("v4l4j");
		} catch (UnsatisfiedLinkError e) {
			System.err.println("Cant load v4l4j JNI library");
			throw e;
		}
	}
	
	protected final long object;
	
	protected final int csp;
	protected final int width;
	protected final int height;
	/**
	 * Allocates native struct and initializes it.
	 * @param csp
	 * @param width
	 * @param height
	 * @return
	 */
	protected native long init(int csp, int width, int height);
	
	@Override
	public native void close();
	
	public H264Picture(int csp, int width, int height) {
		this.csp = csp;
		this.width = width;
		this.height = height;
		this.object = init(csp, width, height);
	}
	
	public int getCsp() {
		return csp;
	}
	public int getWidth() {
		return width;
	}
	public int getHeight() {
		return height;
	}

	@Override
	public FrameGrabber getFrameGrabber() {
		// TODO Auto-generated method stub
		return null;
	}

	@Override
	public int getFrameLength() {
		// TODO Auto-generated method stub
		return 0;
	}

	@Override
	public long getSequenceNumber() {
		// TODO Auto-generated method stub
		return 0;
	}

	@Override
	public long getCaptureTime() {
		// TODO Auto-generated method stub
		return 0;
	}

	@Override
	public byte[] getBytes() {
		// TODO Auto-generated method stub
		return null;
	}

	@Override
	public DataBuffer getDataBuffer() {
		// TODO Auto-generated method stub
		return null;
	}

	@Override
	public Raster getRaster() throws UnsupportedMethod {
		// TODO Auto-generated method stub
		return null;
	}

	@Override
	public BufferedImage getBufferedImage() throws UnsupportedMethod {
		// TODO Auto-generated method stub
		return null;
	}

	@Override
	public void recycle() {
		// TODO Auto-generated method stub
		
	}
}