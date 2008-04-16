#!/bin/sh
which curl >  /dev/null
test $? -gt 0 && echo "Curl is not on the path" && exit 1

test -f ~/.smugup && source ~/.smugup

UA="smugup.sh/1.1 (recht@braindump.dk)"
APIKEY="yppE0KMFXm9YwpJHXrrKR5MAHoWZvVaH"


while getopts "a:p:u:" flag; do
    case $flag in
	u)
	    EMAIL=$OPTARG
	    shift;shift;;
	p)
	    PASSWORD=$OPTARG
	    shift;shift;;
	a)
	    ALBUM=$OPTARG
	    shift;shift;;
	*)
	    echo "Usage: $0 [-u email] [-p password] [-a albumId] files..."
	    exit 1
    esac
done

test -z "$EMAIL" && echo "Username missing" && exit 1
test -z "$PASSWORD" && echo "Password missing" && exit 1


test $# -eq 0 && echo "No files given" && exit 1

SID=`curl -A "$UA" -s "https://api.smugmug.com/hack/rest/1.1.1/?method=smugmug.login.withPassword&EmailAddress=$EMAIL&Password=$PASSWORD&APIKey=$APIKEY" | grep SessionID`
SID=${SID/*<SessionID>/}
SID=${SID/<\/SessionID>*/}

test -z $SID && echo "Unable to login" && exit 1

if [ -z "$ALBUM" ]; then

curl -A "$UA" -s "https://api.smugmug.com/hack/rest/1.1.1/?method=smugmug.albums.get&SessionID=$SID&APIKey=$APIKEY"| \
awk -F'[<\"][^>\"]*[>\"]' \
'BEGIN { ORS="" } /Album / {album = $2; p = 1}; /Title/ {if (p) { print " " $2 ":  " album "\n"; p = 0 } }' \
 | sort -n

echo
echo

read -p "Album ID: " ALBUM

fi

OUT=`tempfile`
for i in "$@"; do
	echo "Uploading $i"
	MD5=`md5sum -b "$i" | awk '{print $1}'`
	FN=`basename "$i"`
	curl -A "$UA" -H "Content-MD5: $MD5" -H "X-Smug-SessionID: $SID" -H "X-Smug-Version: 1.1.1" -H "X-Smug-ResponseType: REST" -H "X-Smug-AlbumID: $ALBUM" -T "$i" -o $OUT http://upload.smugmug.com/$FN
	echo
	grep -q "fail" $OUT
	if [ $? -eq 0 ]; then
	    sed -n '/<err/ {
		s/.*msg="\(.*\)".*/Upload failed: \1/
		p
		D
	    }' $OUT
	fi
done

rm -f $OUT

curl -s -o /dev/null -A "$UA" "https://api.smugmug.com/hack/rest/1.1.1/?method=smugmug.logout&SessionID=$SID&APIKey=$APIKEY"
