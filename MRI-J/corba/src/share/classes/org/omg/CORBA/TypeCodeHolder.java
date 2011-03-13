/*
 * Copyright 1996-2001 Sun Microsystems, Inc.  All Rights Reserved.
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

package org.omg.CORBA;

import org.omg.CORBA.portable.Streamable;
import org.omg.CORBA.portable.InputStream;
import org.omg.CORBA.portable.OutputStream;

/**
 * The Holder for <tt>TypeCode</tt>.  For more information on
 * Holder files, see <a href="doc-files/generatedfiles.html#holder">
 * "Generated Files: Holder Files"</a>.<P>
 * A Holder class for a <code>TypeCode</code> object
 * that is used to store "out" and "inout" parameters in IDL operations.
 * If an IDL operation signature has an IDL <code>TypeCode</code> as an "out"
 * or "inout" parameter, the programmer must pass an instance of
 * <code>TypeCodeHolder</code> as the corresponding
 * parameter in the method invocation; for "inout" parameters, the programmer
 * must also fill the "in" value to be sent to the server.
 * Before the method invocation returns, the ORB will fill in the
 * value corresponding to the "out" value returned from the server.
 * <P>
 * If <code>myTypeCodeHolder</code> is an instance of <code>TypeCodeHolder</code>,
 * the value stored in its <code>value</code> field can be accessed with
 * <code>myTypeCodeHolder.value</code>.
 *
 * @since       JDK1.2
 */
public final class TypeCodeHolder implements Streamable {

    /**
     * The <code>TypeCode</code> value held by
     * this <code>TypeCodeHolder</code> object.
     */
    public TypeCode value;

    /**
     * Constructs a new <code>TypeCodeHolder</code> object with its
     * <code>value</code> field initialized to <code>null</code>.
     */
    public TypeCodeHolder() {
    }

    /**
     * Constructs a new <code>TypeCodeHolder</code> object with its
     * <code>value</code> field initialized to the given
     * <code>TypeCode</code> object.
     * @param initial the <code>TypeCode</code> object with which to initialize
     *                the <code>value</code> field of the newly-created
     *                <code>TypeCodeHolder</code> object
     */
    public TypeCodeHolder(TypeCode initial) {
        value = initial;
    }

    /**
     * Reads from <code>input</code> and initalizes the value in
     * this <code>TypeCodeHolder</code> object
     * with the unmarshalled data.
     *
     * @param input the InputStream containing CDR formatted data from the wire
     */
    public void _read(InputStream input) {
        value = input.read_TypeCode();
    }

    /**
     * Marshals to <code>output</code> the value in
     * this <code>TypeCodeHolder</code> object.
     *
     * @param output the OutputStream which will contain the CDR formatted data
     */
    public void _write(OutputStream output) {
        output.write_TypeCode(value);
    }

    /**
     * Returns the TypeCode corresponding to the value held in
     * this <code>TypeCodeHolder</code> object.
     *
     * @return    the TypeCode of the value held in
     *             this <code>TypeCodeHolder</code> object
     */
    public org.omg.CORBA.TypeCode _type() {
        return ORB.init().get_primitive_tc(TCKind.tk_TypeCode);
    }
}