/*
 * Copyright 1997-1999 Sun Microsystems, Inc.  All Rights Reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   - Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *   - Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 *   - Neither the name of Sun Microsystems nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */



/**
 * A minimal example, using the JTable to view data from a database.
 *
 * @author Philip Milne
 */

import javax.swing.*;

import java.awt.event.WindowAdapter;
import java.awt.event.WindowEvent;
import java.awt.Dimension;

public class TableExample2 {

    public TableExample2(String URL, String driver, String user,
                         String passwd, String query) {
        JFrame frame = new JFrame("Table");
        frame.addWindowListener(new WindowAdapter() {
            public void windowClosing(WindowEvent e) {System.exit(0);}});
        JDBCAdapter dt = new JDBCAdapter(URL, driver, user, passwd);
        dt.executeQuery(query);

        // Create the table
        JTable tableView = new JTable(dt);

        JScrollPane scrollpane = new JScrollPane(tableView);
        scrollpane.setPreferredSize(new Dimension(700, 300));

        frame.getContentPane().add(scrollpane);
        frame.pack();
        frame.setVisible(true);
    }

    public static void main(String[] args) {
        if (args.length != 5) {
            System.err.println("Needs database parameters eg. ...");
            System.err.println("java TableExample2 \"jdbc:sybase://dbtest:1455/pubs2\" \"connect.sybase.SybaseDriver\" guest trustworthy \"select * from titles\"");
            return;
        }
        new TableExample2(args[0], args[1], args[2], args[3], args[4]);
    }
}
