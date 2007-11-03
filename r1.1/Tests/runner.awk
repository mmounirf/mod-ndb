
BEGIN { if(!host) host = "localhost:3080" 
        server = "http://" host 
        if(test ~ /^-/) test = ""
        echo = (mode == "compare") ? "echo -n" : "echo";
        
        recorder = "awk -f record.awk -v obj="
      }

/^#/  { next; }
/^$/  { next; }

{ if( (!test) || ($1 ~ test)) {
     sub(/#.*/, "")  # strip comments

     flag_SQL = flag_JR = 0  
     filter = args = ""
     
     # Get the flags
     split($2, flags, "/")
     for(i in flags) {
       if(flags[i] == "SQL") flag_SQL = 1
       else if(flags[i] ~ /^f/) filter = "sed -f " flags[i] ".sed"
       else if(flags[i] == "JR") flag_JR = 1
     }
  
     if(mode == "sql" && flag_SQL) {
        printf("mysql --defaults-file=my.cnf < SQL/%s \n\n",$3)
        next
     }
     else if(mode == "list") { 
        print 
        next 
     }
     else if(mode == "config") {
        qm = index($3, "?") ; 
        if (qm) base = substr($3, 0, qm - 1) ">"
        else base = $3 ">"
        printf("awk -f config.awk -v 'cfpat=/ndb/test/%s' httpd.conf\n", base)
        next
     }

     if(flag_SQL) next;
     
     prefix = substr($1,0,3)
     archive_file = "results/" prefix ".archive"
     
     if(mode == "compare")      outfile = "> current/" $1
     else if(mode == "record")  outfile = "| " recorder $1 " >> " archive_file
     else outfile = " | tee current/" $1
          
     if(NF > 3) for(i=4; i <= NF; i++) args = args $i " ";
     if(flag_JR) args = args " -H 'Content-Type: application/jsonrequest' "

     cmd = sprintf("curl -isS %s '%s/ndb/test/%s'", args, server, $3)

     printf("%s '==== %s '\n", echo, $1)
     printf("%s | %s %s \n", cmd, filter, outfile)

     if((diff || mode == "compare") && ! have_source[prefix]) {
       print "source " archive_file
       have_source[prefix] = 1
     }
     
     if(mode == "compare") 
       printf("r.%s | cmp -s - current/%s && echo OK || echo Fail\n", $1, $1)
     if(diff)
       printf("r.%s |diff -C 10 - current/%s \n",$1, $1)
   }
}