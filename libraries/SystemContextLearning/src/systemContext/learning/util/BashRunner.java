/**
 * "BashRunner" Java class is free software: you can redistribute
 * it and/or modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version always keeping
 * the additional terms specified in this license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 *
 * Additional Terms of this License
 * --------------------------------
 *
 * 1. It is Required the preservation of specified reasonable legal notices
 *   and author attributions in that material and in the Appropriate Legal
 *   Notices displayed by works containing it.
 *
 * 2. It is limited the use for publicity purposes of names of licensors or
 *   authors of the material.
 *
 * 3. It is Required indemnification of licensors and authors of that material
 *   by anyone who conveys the material (or modified versions of it) with
 *   contractual assumptions of liability to the recipient, for any liability
 *   that these contractual assumptions directly impose on those licensors
 *   and authors.
 *
 * 4. It is Prohibited misrepresentation of the origin of that material, and it is
 *   required that modified versions of such material be marked in reasonable
 *   ways as different from the original version.
 *
 * 5. It is Declined to grant rights under trademark law for use of some trade
 *   names, trademarks, or service marks.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program (lgpl.txt).  If not, see <http://www.gnu.org/licenses/>
 */

package systemContext.learning.util;

import java.io.IOException;
import java.io.InputStream;

/**
 * This class represents common actions or functions which can be useful for 
 * other Java classes of the same package.
 * 
 * @author UPM (member of RECLAMO Development Team)(http://reclamo.inf.um.es)
 * @version 1.0
 */
public class BashRunner {

       public BashRunner(String comando){
 
           try
            {
            String command;
            command=comando;
            //command="mount -h";
            final Process process = Runtime.getRuntime().exec(command);
            new Thread(){
                @Override
                public void run(){
                    try{
                        InputStream is = process.getInputStream();
                        byte[] buffer = new byte[1024];
                        for(int count = 0; (count = is.read(buffer)) >= 0;){
                            System.out.write(buffer, 0, count);
                        }
                    }
                    catch(Exception e){
                     e.printStackTrace();
                    }
                }
            }.start();
            new Thread(){
                @Override
                public void run(){
                    try{
                        InputStream is = process.getErrorStream();
                        byte[] buffer = new byte[1024];
                        for(int count = 0; (count = is.read(buffer)) >= 0;){
                            System.err.write(buffer, 0, count);
                        }
                    }
                    catch(Exception e){
                    e.printStackTrace();
                    }
                }
            }.start();
            int returnCode = process.waitFor();
            //System.out.println("Return code = " + returnCode);
            }
            catch (Exception e){
            e.printStackTrace();
            }
   }
}
