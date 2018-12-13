all:
	gcc -Wall -Wno-parentheses -Os eval.c parser.c print.c run.c util.c -lm -o ddb

bu:
	cd ..; rsync -av basic bait:
tar:
	cd ..; tar caf /tmp/basic.tar.gz basic
	scp /tmp/basic.tar.gz coburn:/var/www/wakesecure.com/
