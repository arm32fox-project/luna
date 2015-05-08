/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

// Make sure that there are telemetry entries created by sqlite io

function run_sql(d, sql) {
  var stmt = d.createStatement(sql)
  stmt.execute()
  stmt.finalize();
}

function new_file(name)
{
  var file = dirSvc.get("ProfD", Ci.nsIFile);
  file.append(name);
  return file;
}
function run_test()
{
  // Telemetry stub
}

