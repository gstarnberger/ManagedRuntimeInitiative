/*
 * Copyright 2007 Sun Microsystems, Inc.  All Rights Reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.  Sun designates this
 * particular file as subject to the "Classpath" exception as provided
 * by Sun in the LICENSE file that accompanied this code.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
 * CA 95054 USA or visit www.sun.com if you need additional information or
 * have any questions.
 */

/* @test
   @summary Test SoftTuning load method */

import java.io.UnsupportedEncodingException;

import javax.sound.midi.MidiUnavailableException;
import javax.sound.midi.Patch;
import javax.sound.sampled.*;

import com.sun.media.sound.*;

public class Load1 {
	
	private static void assertEquals(Object a, Object b) throws Exception
	{
		if(!a.equals(b))
			throw new RuntimeException("assertEquals fails!");
	}	
	
	private static void assertTrue(boolean value) throws Exception
	{
		if(!value)
			throw new RuntimeException("assertTrue fails!");
	}
		
	public static void main(String[] args) throws Exception {
		// http://www.midi.org/about-midi/tuning.shtml
		// 0x01 BULK TUNING DUMP
		SoftTuning tuning = new SoftTuning();
		byte[] name;
		name = "Testing123      ".getBytes("ascii");
		
		int[] msg = new int[24+3*128];		
		int[] head = {0xf0,0x7e,0x7f,0x08,0x01,0x00};
		int ox = 0;
		for (int i = 0; i < head.length; i++) 
			msg[ox++] = head[i];
		for (int i = 0; i < name.length; i++) 
			msg[ox++] = name[i];
		for (int i = 0; i < 128; i++) {
			msg[ox++] = i;
			msg[ox++] = 64;
			msg[ox++] = 0;
		}
		
		// Calc checksum
		int x = msg[1] & 0xFF; // 7E
		x = x ^ (msg[2] & 0xFF); // <device ID>
		x = x ^ (msg[4] & 0xFF); // nn
		x = x ^ (msg[5] & 0xFF); // tt
		for (int i = 22; i < msg.length - 2; i++)
			x = x ^ (msg[i] & 0xFF);
		msg[ox++] = (x & 127);
				
		msg[ox++] = 0xf7;
		byte[] bmsg = new byte[msg.length];
		for (int i = 0; i < bmsg.length; i++) 
			bmsg[i] = (byte)msg[i];
		
		tuning.load(bmsg);
		assertEquals(tuning.getName(), "Testing123      ");
		double[] tunings = tuning.getTuning();
		for (int i = 0; i < tunings.length; i++) 	
			assertTrue(Math.abs(tunings[i]-(i*100 + 50)) < 0.00001);		
		
		// Check if tuning fails if checksum is wrong
		/*
		msg[msg.length - 2] += 10;
		for (int i = 0; i < bmsg.length; i++) 
			bmsg[i] = (byte)msg[i];
		tuning = new SoftTuning();
		tuning.load(bmsg);
		assertTrue(!tuning.getName().equals("Testing123      "));
		*/
	}
}
