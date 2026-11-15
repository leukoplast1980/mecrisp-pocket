: sq dup * ;
( a comment )
\ a line comment

7 sq .
: clamp ( n -- n )
  dup 0< if
    drop 0
  then
;
-5 clamp . 9 clamp .
