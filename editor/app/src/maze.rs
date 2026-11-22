//! The maze as a grid of cells. Saved as text: one line per row, north first,
//! `#` for a wall and `.` for floor.

#[derive(Clone, Debug, PartialEq)]
pub struct Maze {
    pub rows: usize,
    pub cols: usize,
    walls: Vec<bool>,
}

impl Maze {
    /// A maze of `rows` by `cols` with walls all around the edge.
    pub fn bordered(rows: usize, cols: usize) -> Self {
        let walls = (0..rows * cols)
            .map(|i| {
                let (row, col) = (i / cols, i % cols);
                row == 0 || col == 0 || row == rows - 1 || col == cols - 1
            })
            .collect();
        Maze { rows, cols, walls }
    }

    /// Read the text form. Every row must be the same length.
    pub fn parse(text: &str) -> Result<Self, String> {
        let lines: Vec<&str> = text.lines().filter(|line| !line.is_empty()).collect();
        let cols = lines.first().map_or(0, |line| line.chars().count());
        if cols == 0 {
            return Err("the maze is empty".into());
        }
        let mut walls = Vec::with_capacity(lines.len() * cols);
        for (row, line) in lines.iter().enumerate() {
            if line.chars().count() != cols {
                return Err(format!("row {} has {} cells, expected {cols}", row + 1, line.chars().count()));
            }
            for (col, cell) in line.chars().enumerate() {
                match cell {
                    '#' => walls.push(true),
                    '.' => walls.push(false),
                    other => return Err(format!("row {} column {} is '{other}', expected '#' or '.'", row + 1, col + 1)),
                }
            }
        }
        Ok(Maze { rows: lines.len(), cols, walls })
    }

    /// The text form, ending with a newline.
    pub fn to_text(&self) -> String {
        let mut text = String::with_capacity(self.rows * (self.cols + 1));
        for row in 0..self.rows {
            for col in 0..self.cols {
                text.push(if self.is_wall(row, col) { '#' } else { '.' });
            }
            text.push('\n');
        }
        text
    }

    pub fn is_wall(&self, row: usize, col: usize) -> bool {
        self.walls[row * self.cols + col]
    }

    pub fn toggle(&mut self, row: usize, col: usize) {
        let cell = &mut self.walls[row * self.cols + col];
        *cell = !*cell;
    }
}

#[cfg(test)]
mod tests {
    use super::Maze;

    #[test]
    fn bordered_round_trips() {
        let maze = Maze::bordered(3, 4);
        assert_eq!(maze.to_text(), "####\n#..#\n####\n");
        assert_eq!(Maze::parse(&maze.to_text()), Ok(maze));
    }

    #[test]
    fn toggle_flips_one_cell() {
        let mut maze = Maze::bordered(3, 3);
        maze.toggle(1, 1);
        assert_eq!(maze.to_text(), "###\n###\n###\n");
    }

    #[test]
    fn rejects_bad_text() {
        assert!(Maze::parse("").is_err());
        assert!(Maze::parse("##\n#\n").is_err());
        assert!(Maze::parse("#x\n").is_err());
    }
}
