/*
 * Copyright (c) 2003-2006 Goetz Schwandtner. All Rights Reserved.
 */

import java.util.Random;
import java.lang.Integer;
import java.lang.Math;
import java.util.Date;
import javax.microedition.lcdui.*;
import javax.microedition.midlet.*;

/**
 * KoalaMines -- Another Mine Sweeper Clone
 */

/**
 * (c) by Goetz Schwandtner, schwandtner@googlemail.com
 *
 * All rights reserved.
 * 
 * This program is free software and intended to be a quick and very dirty
 * implementation of the famous game on the J2ME platform. 
 * Intended features were
 *      - low resource usage, ie small file size and quick execution
 *      - highly configurable playing field, including different a scaling
 *        feature allowing very big fields on small screens
 *      - lazy gaming using a half-automatic mine searcher findig all 
 *        non-ambigous mines -- good for big fields ;)
 *
 * Enjoy this game.
 */
 
public class KoalaMines extends MIDlet implements CommandListener
{
   static final String VERSION="1.1";

   static final String EXIT_COMMAND_LABEL = "Quit";
   static final String OPTIONS_COMMAND_LABEL = "Options";

   static final String MINES_COMMAND_LABEL = " left";
   static final String MINES_COMMAND_LOST = "Oops! :-(";
   static final String MINES_COMMAND_WON = "Good job! :-)";

   static final String RESET_COMMAND_LABEL = "Reset";
   static final String HELP_COMMAND_LABEL = "Help";
   static final String ABOUT_COMMAND_LABEL = "About";
   public Display display;
   public KoalaCanvas canvas;
   KoalaOptions opt;
   Command minesCommand;
   
   public void destroyApp (boolean b)
   {
      display.setCurrent(null);
      this.notifyDestroyed();       // notify KVM
   }

   protected void pauseApp () 
   { }

   protected void startApp ()
   {
      canvas = new KoalaCanvas(this);
      opt = new KoalaOptions(this, canvas);
	
      display = Display.getDisplay(this);

      Command resetCommand = new Command(RESET_COMMAND_LABEL, Command.SCREEN, 1);
      Command optionsCommand = new Command(OPTIONS_COMMAND_LABEL, Command.SCREEN, 2);
      Command helpCommand = new Command(HELP_COMMAND_LABEL, Command.SCREEN, 3);
      Command aboutCommand = new Command(ABOUT_COMMAND_LABEL, Command.SCREEN, 4);
      Command exitCommand = new Command(EXIT_COMMAND_LABEL , Command.SCREEN, 5);

      minesCommand = new Command(MINES_COMMAND_LABEL, Command.BACK, 0);
      
      canvas.addCommand(minesCommand);
      canvas.addCommand(resetCommand);
      canvas.addCommand(optionsCommand);
      canvas.addCommand(helpCommand);
      canvas.addCommand(aboutCommand);
      canvas.addCommand(exitCommand);
      canvas.setCommandListener(this);

      display.setCurrent(canvas);  
   }

   public void commandAction (Command c, Displayable d)
   {
       String label = c.getLabel();
       if (label == EXIT_COMMAND_LABEL ) {
          destroyApp(false);
       } else if (label == HELP_COMMAND_LABEL ) {
	Alert a = new Alert("KoalaMines Help", "Move cursor: 1,2,3,4,6,7,8,9; \n"+
	"Open field: 0;\nSet Flag: #.\nSet Flag, open flagged field: 5.\nAutomatic searcher: *",
	 null, AlertType.INFO);
	a.setTimeout(Alert.FOREVER);
        display.setCurrent(a);
       } else if ((label == RESET_COMMAND_LABEL) ||
		  (label == MINES_COMMAND_LOST) ||
		  (label == MINES_COMMAND_WON)) {
	  canvas.reset(); 
       } else if (label == OPTIONS_COMMAND_LABEL ) {
	  display.setCurrent(opt);
       } else if (label == ABOUT_COMMAND_LABEL ) {
       	Alert a = new Alert("KoalaMines", "(c) 2003-2006 by Goetz Schwandtner <schwandtner@googlemail.com> Version: "+VERSION , null, AlertType.INFO);
	a.setTimeout(Alert.FOREVER);
        display.setCurrent(a);
       } 
   }
}

class KoalaCanvas extends Canvas
{
   private KoalaMines koala;
   static Random rand;

   class cell_t { public boolean open; 
		  public boolean mine; 
		  public boolean flag; 
		  public boolean flood; 
      public cell_t() {
	open= mine= flag= flood= false;
      }
   }
   
   private cell_t field[][];
   private int width;
   private int height;
   private int sc; // scale (magnifying factor) new in 0.7.8

   private int xpos;
   private int ypos;

   private boolean drawMove;

   private int mines;
   private int mines_left;
   private int old_flags_left;
   private int flags_left;

   private int mines_nhd[][];

   boolean foundAll= false;
   boolean gameOver= false;
   boolean showEndScreen= true;

   private Date startTime= null;
   private Date endTime= null;

   private Font endScreenFont;
   private final int END_SCREEN_BORDER= 5;
   private final String LOST_LABEL="Oops. Hit a mine!";
   private final String WON_LABEL="Good job! Found all!";

   private Image buf;
   private Graphics bufgr;

   public KoalaCanvas( KoalaMines k )
   {
	koala=k;
	rand= new Random();
	buf= Image.createImage(getWidth(), getHeight());
	bufgr= buf.getGraphics();
	endScreenFont= Font.getFont(Font.FACE_PROPORTIONAL,
		Font.STYLE_BOLD, Font.SIZE_MEDIUM);
   }

   public void setValues(int x, int y, int m, int s)
   {
	width=x; height=y; mines=m; sc=s;	
	reset();
   }

   public void reset()
   {
	init_field(width,height);
	put_mines(mines);
	xpos=1; ypos=1;	
	foundAll= false;
   	gameOver= false;
	showEndScreen= true;
	drawMove= false;
	startTime= new Date();
	endTime= null;
	repaint();
   }

   private void paint_cell (Graphics g, int x, int y)
   {
      int x1=sc*((x-1)*4+1);
      int y1=sc*((y-1)*6+1);

      if ( !gameOver && !foundAll &&
	   xpos==x && ypos==y) {   // draw selected field
      	bufgr.setColor(0,0,0);
	bufgr.fillRect(x1-sc,y1-sc,sc*5,sc*7); // let the gui control the clipping	
      }	
      if (drawMove) {
	bufgr.setColor(255,255,255);
	bufgr.fillRect(x1-sc,y1-sc,sc*5,sc*7); // let the gui control the clipping
      }		
      if (field[x][y].open || gameOver || 
	  (foundAll && !field[x][y].mine)) {
	if (field[x][y].mine) {
	   g.setColor(0,0,0);     // draw a mine
	   g.fillRect(x1,y1,sc*3,sc*5);
	   g.setColor(150,15,15);
	   g.fillRect(x1,y1+sc,sc,sc*3);
	   g.fillRect(x1+sc,y1,sc,sc*5);
	   g.fillRect(x1+sc*2,y1+sc,sc,sc*3);
	   g.setColor(175,175,30);
	   g.fillRect(x1+sc,y1+sc,sc,sc);
	   g.fillRect(x1,y1+sc*2,sc,sc);
	   g.fillRect(x1+sc,y1+sc*3,sc,sc);
	   g.fillRect(x1+sc*2,y1+sc*2,sc,sc);
	} else {
	     switch(mines_nhd[x][y]) {
		case 1: g.setColor(90,90,255);
		   g.fillRect(x1+sc,y1,sc,sc*5);
		   break;
		case 2: g.setColor(40,180,40);
		   g.fillRect(x1,y1,sc*3,sc);
	   	   g.fillRect(x1+sc*2,y1,sc,sc*3);
		   g.fillRect(x1,y1+sc*2,sc*3,sc);
		   g.fillRect(x1,y1+sc*2,sc,sc*3);
		   g.fillRect(x1,y1+sc*4,sc*3,sc);
		   break;
		case 3: g.setColor(180,40,40);
		   g.fillRect(x1,y1,sc*3,sc);
		   g.fillRect(x1,y1+sc*2,sc*3,sc);
		   g.fillRect(x1,y1+sc*4,sc*3,sc);
		   g.fillRect(x1+sc*2,y1,sc,sc*5);
		   break;
		case 4: g.setColor(180,40,180);
		   g.fillRect(x1,y1,sc,sc*3);
		   g.fillRect(x1,y1+sc*2,sc*2,sc);
		   g.fillRect(x1+sc*2,y1,sc,sc*5);
		   break;
		case 5: g.setColor(180,40,180);
		   g.fillRect(x1,y1,sc*3,sc);
		   g.fillRect(x1,y1,sc,sc*3);
		   g.fillRect(x1,y1+sc*2,sc*3,sc);
		   g.fillRect(x1+sc*2,y1+sc*2,sc,sc*3);
		   g.fillRect(x1,y1+sc*4,sc*3,sc);
		   break;
		case 6: g.setColor(180,40,180);
		   g.fillRect(x1,y1,sc*3,sc);
		   g.fillRect(x1,y1+sc*2,sc*3,sc);
		   g.fillRect(x1,y1+sc*4,sc*3,sc);
		   g.fillRect(x1,y1,sc,sc*5);
		   g.fillRect(x1+sc*2,y1+sc*2,sc,sc*3);
		   break;
		case 7: g.setColor(180,40,180);
		   g.fillRect(x1,y1,sc*3,sc);
		   g.fillRect(x1+sc*2,y1,sc,sc*5);
		   break;
		case 8: g.setColor(180,40,180);
		   g.fillRect(x1,y1,sc*3,sc);
		   g.fillRect(x1,y1+sc*2,sc*3,sc);
		   g.fillRect(x1,y1+sc*4,sc*3,sc);
		   g.fillRect(x1,y1,sc,sc*5);
		   g.fillRect(x1+sc*2,y1,sc,sc*5);
		   break;
	     }
	}
      } else {
	g.setColor(200,200,200);   // field background
	g.fillRect(x1,y1,3*sc,5*sc);
	if (field[x][y].flag) {
		g.setColor(15,15,150); // flagpole
		g.fillRect(x1,y1,sc,sc*5);
		if (foundAll)
			g.setColor(25,170,25); 
		else	
			g.setColor(150,15,15);
		g.fillRect(x1+sc,y1,2*sc,3*sc);
	}
      } 
   }

   // draw the field
   public void paint (Graphics g)
   {
	foundAll= (mines_left == 0) && (flags_left == 0);
	if (drawMove && !foundAll) {
	    for (int i=xpos-1; i<xpos+2; i++)
		for (int j=ypos-1; j<ypos+2; j++)		
	    		paint_cell(bufgr, 
				   (i-1+width) %width +1, 
				   (j-1+height)%height +1);
	   drawMove= false;
	   paint_cell(bufgr, xpos, ypos);
	} else {
	    bufgr.setColor(255,255,255);
	    bufgr.fillRect(0,0,getWidth(),getHeight());
	    for (int i=1; i<width+1; i++)
	       for (int j=1; j<height+1; j++)
		   paint_cell(bufgr,i,j);
	}
	displayEndScreen(bufgr);
	g.drawImage(buf,0,0,Graphics.TOP|Graphics.LEFT);
	updateMineCount();
   }

   private void displayEndScreen(Graphics g) {
	if (!showEndScreen)
	   return;
	if (foundAll) {
	   String timeLabel="Time needed: "+usedTime();
	   int w=Math.max(endScreenFont.stringWidth(WON_LABEL),
		endScreenFont.stringWidth(timeLabel))+2*END_SCREEN_BORDER;
	   int h=2*endScreenFont.getHeight()+3*END_SCREEN_BORDER;
	   g.setColor(80,230,100);
	   g.fillRoundRect(getWidth()/2-w/2,getHeight()/2-h/2,w,h,
		END_SCREEN_BORDER,END_SCREEN_BORDER);
	   g.setColor(0,0,0);
	   g.drawRoundRect(getWidth()/2-w/2,getHeight()/2-h/2,w,h,
		END_SCREEN_BORDER,END_SCREEN_BORDER);
	   g.drawString(WON_LABEL,getWidth()/2-w/2+END_SCREEN_BORDER,
			getHeight()/2-h/2+END_SCREEN_BORDER,
			Graphics.LEFT|Graphics.TOP);
	   g.drawString(timeLabel,
			getWidth()/2-w/2+END_SCREEN_BORDER,
			getHeight()/2-h/2+2*END_SCREEN_BORDER+
			endScreenFont.getHeight(),
			Graphics.LEFT|Graphics.TOP);
	} else if (gameOver) {
	   String timeLabel="Time needed: "+usedTime();
	   int w=Math.max(endScreenFont.stringWidth(WON_LABEL),
		endScreenFont.stringWidth(timeLabel))+2*END_SCREEN_BORDER;
	   int h=2*endScreenFont.getHeight()+3*END_SCREEN_BORDER;
	   g.setColor(240,100,100);
	   g.fillRoundRect(getWidth()/2-w/2,getHeight()/2-h/2,w,h,
		END_SCREEN_BORDER,END_SCREEN_BORDER);
	   g.setColor(0,0,0);
	   g.drawRoundRect(getWidth()/2-w/2,getHeight()/2-h/2,w,h,
		END_SCREEN_BORDER,END_SCREEN_BORDER);
	   g.drawString(LOST_LABEL,getWidth()/2-w/2+END_SCREEN_BORDER,
			getHeight()/2-h/2+END_SCREEN_BORDER,
			Graphics.LEFT|Graphics.TOP);
	   g.drawString(timeLabel,
			getWidth()/2-w/2+END_SCREEN_BORDER,
			getHeight()/2-h/2+2*END_SCREEN_BORDER+
			endScreenFont.getHeight(),
			Graphics.LEFT|Graphics.TOP);
	}
   }

   private String usedTime() {
	if (endTime == null)
	  endTime=new Date();
	int diff=(int)(endTime.getTime()-startTime.getTime())/1000;
	String st= (diff/60)+":";
	if (diff%60<10)
	  st += "0";
	return st+(diff%60);
   }

   private void updateMineCount() {
	String st;
	if (foundAll)
	 	st= koala.MINES_COMMAND_WON;
	else if (gameOver)
		st= koala.MINES_COMMAND_LOST;
	else if (old_flags_left != flags_left) {
		old_flags_left= flags_left;
		st= flags_left+" "+koala.MINES_COMMAND_LABEL; 
	} else 
	   return;
	removeCommand(koala.minesCommand);
      	koala.minesCommand = new Command(st , Command.BACK, 0);      
      	addCommand(koala.minesCommand);
   }
   // initialize field
   private void init_field(int w, int h)
   {
      width=w; height=h;
      
      field= new cell_t[w+2][];

      for (int i=0; i < w+2; i++) {
	field[i]= new cell_t[h+2];
        for (int j=0; j < h+2; j++) 
          field[i][j] = new cell_t();
	field[i][0].open = field[i][h+1].open = true;
	}
      for (int j=0; j < h+2; j++) 
	field[0][j].open = field[w+1][j].open = true;
   }


   // sum of mines in neighbourhood
   private int mines_nhdc(int x, int y)
   {
     int sum=0;
     for (int i=x-1; i < x+2; i++)
       for (int j=y-1; j < y+2; j++)
         sum += field[i][j].mine ? 1 : 0; 
     return sum;
   }
 
   // throw mines onto the playing field
   private void put_mines( int num )
   {
     flags_left= mines_left= mines= num;
     old_flags_left= -1;	
     while (num>0) {
        int y=1+Math.abs(rand.nextInt())%height;
        int x=1+Math.abs(rand.nextInt())%width;
        if (!field[x][y].mine) {
          field[x][y].mine= true;
          num--;
        }
     }
     
     mines_nhd = new int[width+1][];
     for (int i=1; i<width+1; i++) {
       mines_nhd[i] = new int[height+1];
       for (int j=1; j<height+1; j++)
         mines_nhd[i][j]= mines_nhdc(i,j);
     }
   }  

   // sum of flags in neighbourhood
   int flags_nhd(int x, int y)
   {
     int sum=0;
     for (int i=x-1; i < x+2; i++)
       for (int j=y-1; j < y+2; j++)
         sum += field[i][j].flag ? 1 : 0;
     return sum;
   }

   // sum of closed fields in the neighbourhood
   int closed_nhd(int x, int y)
   {
     int sum=0;
     for (int i=x-1; i < x+2; i++)
       for (int j=y-1; j < y+2; j++)
         sum += field[i][j].open ? 0 : 1;
     return sum;
   }

   // open a non-flagged field; continue with surrounding fields
   void open_cell(int x, int y)
   {
     if ( (x<1) || (y<1) || (x>width) || (y>height) || 
         field[x][y].flag || field[x][y].open )
       return;

     if (field[x][y].mine) {
       gameOver= true;
     } else {
       field[x][y].open= true;
       if (flags_nhd(x,y)==mines_nhd[x][y])
         for (int i=x-1; i < x+2; i++)
	   for (int j=y-1; j < y+2; j++)
	     open_cell(i,j);
     }
   }

   void cleanup_r(int x, int y)
   {
     if ( (x<1) || (y<1) || (x>width) || (y>height) || 
          field[x][y].flag || !field[x][y].open || field[x][y].flood )
       return;

     field[x][y].flood= true;

     field[x][y].open= false;
     open_cell(x,y);

     if (closed_nhd(x,y)==mines_nhd[x][y]) {
       for (int i=x-1; i < x+2; i++)
         for (int j=y-1; j < y+2; j++)
	  if (!field[i][j].open) {
	    if (!field[i][j].flag ) {
		if (field[i][j].mine)
		  mines_left--;
       	      flags_left--;
	      field[i][j].flag= true;
            }
	  }

       for (int i=x-1; i < x+2; i++)
         for (int j=y-1; j < y+2; j++)
	   cleanup_r(i,j);
     }
   }

   void cleanup(int x, int y)
   {
     for (int i=1; i<width+2; i++)
       for (int j=1; j<height+2; j++)
         field[i][j].flood= false;
  
     cleanup_r(x,y);
   }
  	
   protected void keyPressed( int keyCode )
   {
	if (gameOver || foundAll) {
		showEndScreen= !showEndScreen;
		repaint();
		return;
	}

	int key= getGameAction( keyCode );
	switch( key ) {
	case UP:
		ypos= (ypos-2+height) % height +1; 
		drawMove= true;
		repaint();
	break;

	case LEFT: 
		xpos= (xpos-2+width) % width +1; 
		drawMove= true;
		repaint();
	break;
		
	case RIGHT: 
		xpos= xpos % width +1; 
		drawMove= true;
		repaint(); 
	break;

	case DOWN: 
		ypos= ypos % height +1; 
		drawMove= true;
		repaint();
	break;

	case FIRE:
	    if (!field[xpos][ypos].open) {
               if (!field[xpos][ypos].flag) {
	          field[xpos][ypos].flag = true;
		     if (field[xpos][ypos].mine) 
		       mines_left--;
                     flags_left--;
		     drawMove= true;	
               } else {
		     field[xpos][ypos].flag= false;
		     if (field[xpos][ypos].mine) mines_left++;							     flags_left++;
	     	     open_cell(xpos,ypos);
	       } 
            } else {
			field[xpos][ypos].open= false;
			open_cell(xpos,ypos);	         
	    }
	    repaint();
	break;


        default: 
	switch(keyCode) {

	case KEY_NUM1: 
		ypos=(ypos - 2 + height)%height +1;
		xpos=(xpos - 2 + width)%width +1;
		drawMove= true;
		repaint();
	break;
	
	
	case KEY_NUM3: 
		ypos=(ypos - 2 + height)%height +1;
		xpos=xpos%width +1;	
		drawMove= true;
		repaint();
	break;
	
	
	case KEY_NUM0:
		if (!field[xpos][ypos].flag) {
			field[xpos][ypos].open= false;
			open_cell(xpos,ypos);
			repaint();
		}
	break;
	
	case KEY_NUM7:  
		ypos=ypos%height+1;
		xpos=(xpos - 2 + width)%width+1;	
		drawMove= true;
		repaint(); 
	break;
	
	
	case KEY_NUM9:  	
		ypos=ypos%height+1;
		xpos=xpos%width+1;	
		drawMove= true;
		repaint();
	break;
	
	case -11: // GREEN BUTTON	
	case KEY_POUND: if (!field[xpos][ypos].open) {
		field[xpos][ypos].flag = !field[xpos][ypos].flag;
		if (field[xpos][ypos].mine) {
		  if (field[xpos][ypos].flag )
		    mines_left--;
		  else
		    mines_left++;
                }
		if (field[xpos][ypos].flag )
		    flags_left--;
		else
		    flags_left++;
		drawMove= true;
		repaint(); 
	}
	break;

	case KEY_STAR: 
		cleanup(xpos,ypos); repaint(); break; 

	case -12: // RED BUTTON
		koala.destroyApp(false); break;		
	}  
	}
   }
}

class KoalaOptions extends Form implements CommandListener
{
    private int mX;
    private int mY;

    private KoalaMines k;	
    private KoalaCanvas kc;

    private final String OK_COMMAND_LABEL="Ok";
    private final String CANCEL_COMMAND_LABEL="Cancel";
    private final String CHECK_COMMAND_LABEL="Check";
    private final String SETSM_COMMAND_LABEL="Small Field";
    private final String SETBIG_COMMAND_LABEL="Big Field";
    private final String SETDEF_COMMAND_LABEL="Default";
	
	
    private TextField editx;
    private TextField edity;
    private TextField editm;
    private TextField edits;

    private int x;
    private int y;
    private int m;
    private int s;

    public KoalaOptions(KoalaMines koala, KoalaCanvas koalac) {
        super("KoalaMines Options");

	k= koala; kc= koalac;	
	x= mX= (kc.getWidth()-1)/4; 
	y= mY= (kc.getHeight()-1)/6; 
	m= mX*mY/8;
	s=2;	

        editx= new TextField("Field width", Integer.toString(mX), 2, TextField.NUMERIC);
	edity= new TextField("Field height", Integer.toString(mY), 2, 		TextField.NUMERIC);
        editm= new TextField("Number of mines", Integer.toString(m), 3, TextField.NUMERIC);
	edits= new TextField("Scale factor", Integer.toString(s), 1, TextField.NUMERIC);
	
	checkValues(true);
	sendValues();

	append(editx);
	append(edity);
	append(editm);
	append(edits);

      Command okCommand = new Command(OK_COMMAND_LABEL, Command.OK, 0);
      Command cancelCommand = new Command(CANCEL_COMMAND_LABEL, Command.BACK, 1);
      Command checkCommand = new Command(CHECK_COMMAND_LABEL, Command.SCREEN, 2);
      Command setsmCommand = new Command(SETSM_COMMAND_LABEL, Command.SCREEN, 3);
      Command setbigCommand = new Command(SETBIG_COMMAND_LABEL, Command.SCREEN, 4);
      Command setdefCommand = new Command(SETDEF_COMMAND_LABEL, Command.SCREEN, 5);
      
      addCommand(okCommand);
      addCommand(cancelCommand);
      addCommand(checkCommand);
      addCommand(setsmCommand);
      addCommand(setbigCommand);
      addCommand(setdefCommand);
      setCommandListener(this);
    }

    private void sendValues()
    {
	readValues();
        checkValues(false);
	kc.setValues(x, y, m, s);	
    }

    private void checkValues(boolean adaptMineCount)
    {
      s= Math.max(1, Math.min( Math.min(mX, mY) / 3 ,s));
      edits.setString(Integer.toString(s));	

      x= Math.max(3, Math.min(mX / s, x));
      editx.setString(Integer.toString(x));

      y= Math.max(3, Math.min(mY / s, y));
      edity.setString(Integer.toString(y));
 
      if (adaptMineCount) {
        m=x*y/8;
      }	
      m= Math.max(0, Math.min(x*y, m));
      editm.setString(Integer.toString(m));
   }

   private void readValues() 
   {
      s= Integer.parseInt(edits.getString());
      x= Integer.parseInt(editx.getString());
      y= Integer.parseInt(edity.getString());
      m= Integer.parseInt(editm.getString());
   }

   public void commandAction (Command c, Displayable d)
   {
       String label = c.getLabel();
       if (label == OK_COMMAND_LABEL ) {
         try {
	   sendValues();	
	   k.display.setCurrent(kc);
	} catch (Exception e) {
	}
       } else if (label == CANCEL_COMMAND_LABEL ) {
	   k.display.setCurrent(kc);
       } else if (label == CHECK_COMMAND_LABEL ) {
           readValues();
	   checkValues(false);		  
       } else if (label == SETBIG_COMMAND_LABEL ) {
	   x=mX; y=mY; s=1;
	   checkValues(true);
       } else if (label == SETSM_COMMAND_LABEL ) {
	   x=10; y=10; m=10; s=Math.min(mX/10, mY/10);
	   checkValues(false);
       } else if (label == SETDEF_COMMAND_LABEL ) {
           x=mX; y=mY; s=2;
	   checkValues(true);
       }
   }
	
}