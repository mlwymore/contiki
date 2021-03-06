<?php
   /*
    * EMMA Gateway - Network Discovery Utilities Suite
    * Copyright (C) 2010  Clement Duhart - Samuel Martin Moro
    *
    * This program is free software: you can redistribute it and/or modify
    * it under the terms of the GNU General Public License as published by
    * the Free Software Foundation, either version 3 of the License, or
    * (at your option) any later version.
    *
    * This program is distributed in the hope that it will be useful,
    * but WITHOUT ANY WARRANTY; without even the implied warranty of
    * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    * GNU General Public License for more details.
    *
    * You should have received a copy of the GNU General Public License
    * along with this program.  If not, see <http://www.gnu.org/licenses/>.
    *
    * Clement Duhart: <duhart.clement@gmail.com>
    * Samuel Martin Moro: <faust64@gmail.com>
    */

$emma_root = "/opt/emma/";
$emma_root_net_lo = $emma_root."emma-devel/";
$emma_notify_log  = $emma_root."log/notification.log";

$ip_remote = $_SERVER['REMOTE_ADDR'];
$f = fopen($emma_notify_log,"a");

fwrite($f,date("Y-m-d H:i")." New connection : ".$ip_remote."\n");
$node_data_json = file_get_contents("http://[".$ip_remote."]/data/*/");

#fwrite($f,date("Y-m-d H:i")." ".$node_data_json."\n");
$node_data = json_decode($node_data_json);

for ($i = 0; $i < count($node_data); $i++)
{
    $data_file_path = $emma_root_net_lo."{".$ip_remote."}/".$node_data[$i]->name."/data";
    fwrite($f, date("Y-m-d H:i")." Ressource : ".$data_file_path."\n");	  
    $data_file = fopen($data_file_path,"w");

    if(!$data_file)
	fwrite($f, date("Y-m-d H:i")." Fail open local ressource : ".$data_file_path."\n");
    else
    {
	if(!fwrite($data_file,'{ "name":"'.$node_data[$i]->unit.'", "unit":"'.$node_data[$i]->unit.'", "value":"'.$node_data[$i]->value.'" }'))
	    fwrite($f, date("Y-m-d H:i")." Fail writing data\n\n");
	fclose($data_file);	     
    }
}

fclose ($f);
?>
